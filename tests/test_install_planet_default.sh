#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_ROOT="$(mktemp -d)"
trap 'rm -rf "$TEST_ROOT"' EXIT

run_installer() {
  ZT_GLOBAL_BASE="$1" MODPATH="${2:-$ROOT_DIR/module}" \
    ARCH="${ZT_TEST_ARCH-arm64}" IS64BIT="${ZT_TEST_IS64BIT-true}" \
    ZT_TEST_ABIS="${ZT_TEST_ABIS:-}" \
    ZT_GLOBAL_OLD_MODDIR="${3:-$TEST_ROOT/no-active-module}" sh -c '
    ui_print() { printf "%s\n" "$*"; }
    getprop() { [ "$1" = ro.product.cpu.abilist64 ] && printf "%s\n" "$ZT_TEST_ABIS"; }
    . "$1"
  ' sh "$ROOT_DIR/module/customize.sh"
}

# Architecture rejection must happen before creating persistent data or
# invoking any legacy service cleanup.
for unsupported in arm x86 x64 riscv64 unknown; do
  if ZT_TEST_ARCH="$unsupported" run_installer "$TEST_ROOT/rejected-$unsupported" > /dev/null 2>&1; then
    echo "unexpectedly accepted $unsupported" >&2; exit 1
  fi
  [ ! -e "$TEST_ROOT/rejected-$unsupported" ]
done
if ZT_TEST_IS64BIT=false run_installer "$TEST_ROOT/rejected-32bit" > /dev/null 2>&1; then exit 1; fi
[ ! -e "$TEST_ROOT/rejected-32bit" ]
if ZT_TEST_ARCH='' run_installer "$TEST_ROOT/rejected-undetected" > /dev/null 2>&1; then exit 1; fi
[ ! -e "$TEST_ROOT/rejected-undetected" ]
ZT_TEST_ARCH='' ZT_TEST_ABIS=arm64-v8a run_installer "$TEST_ROOT/detected" >/dev/null
[ -f "$TEST_ROOT/detected/config.ini" ]

FRESH_BASE="$TEST_ROOT/fresh"
mkdir -p "$FRESH_BASE/runtime" "$FRESH_BASE/state"
printf 'stale custom planet\n' > "$FRESH_BASE/planet"
printf 'stale legacy roots\n' > "$FRESH_BASE/roots"
printf 'cached custom planet\n' > "$FRESH_BASE/state/roots"
printf 'partial upload\n' > "$FRESH_BASE/runtime/planet-upload-deadbeef.bin"
fresh_output="$(run_installer "$FRESH_BASE")"
[ -f "$FRESH_BASE/config.ini" ]
[ ! -e "$FRESH_BASE/planet" ]
[ ! -e "$FRESH_BASE/roots" ]
[ ! -e "$FRESH_BASE/state/roots" ]
[ ! -e "$FRESH_BASE/runtime/planet-upload-deadbeef.bin" ]
grep -q 'Default Planet: ZeroTier official' <<<"$fresh_output"

UPGRADE_BASE="$TEST_ROOT/upgrade"
LEGACY_MOD="$TEST_ROOT/legacy-module"
STAGED_MOD="$TEST_ROOT/staged-module"
mkdir -p "$UPGRADE_BASE/runtime" "$UPGRADE_BASE/resolver"
mkdir -p "$LEGACY_MOD/system/bin" "$LEGACY_MOD/system/framework"
mkdir -p "$STAGED_MOD"
cp "$ROOT_DIR/module/config/config.example.ini" "$UPGRADE_BASE/config.ini"
printf 'dns_mode=none\nmdns_mode=host\ndns_resolver_enabled=false\n interface_mode = tun\ninterface_mode=tap\n' >> "$UPGRADE_BASE/config.ini"
printf '__unset__\n' > "$UPGRADE_BASE/resolver/mdns-resolution.original"
printf 'invalid\n' > "$UPGRADE_BASE/runtime/resolver-watch.pid"
printf '#!/system/bin/sh\nexit 0\n' > "$LEGACY_MOD/resolver.sh"
printf '#!/system/bin/sh\nexit 0\n' > "$LEGACY_MOD/system/bin/zt-mdnsd"
printf 'legacy resolver bridge\n' > "$LEGACY_MOD/system/framework/zt-resolver-bridge.jar"
chmod 0755 "$LEGACY_MOD/resolver.sh" "$LEGACY_MOD/system/bin/zt-mdnsd"
printf 'preserved custom planet\n' > "$UPGRADE_BASE/planet"
printf 'preserved legacy roots\n' > "$UPGRADE_BASE/roots"
upgrade_output="$(run_installer "$UPGRADE_BASE" "$STAGED_MOD" "$LEGACY_MOD")"
grep -q 'preserved custom planet' "$UPGRADE_BASE/planet"
grep -q 'preserved legacy roots' "$UPGRADE_BASE/roots"
! grep -q '^dns_mode=' "$UPGRADE_BASE/config.ini"
! grep -q '^mdns_mode=' "$UPGRADE_BASE/config.ini"
! grep -q '^dns_resolver_enabled=' "$UPGRADE_BASE/config.ini"
! grep -q 'interface_mode' "$UPGRADE_BASE/config.ini"
[ ! -e "$UPGRADE_BASE/resolver" ]
[ ! -e "$UPGRADE_BASE/runtime/resolver-watch.pid" ]
[ ! -e "$LEGACY_MOD/resolver.sh" ]
[ ! -e "$LEGACY_MOD/system/bin/zt-mdnsd" ]
[ ! -e "$LEGACY_MOD/system/framework/zt-resolver-bridge.jar" ]
grep -q 'Planet setting preserved' <<<"$upgrade_output"

# APatch's installer unzip can flatten private executable modes to 0644.
# The installer must restore all lifecycle scripts and the daemon itself.
PERMISSION_BASE="$TEST_ROOT/permissions"
PERMISSION_MOD="$TEST_ROOT/permission-module"
mkdir -p "$PERMISSION_BASE" "$PERMISSION_MOD/bin" "$PERMISSION_MOD/lib" "$PERMISSION_MOD/config"
cp "$ROOT_DIR/module/config/config.example.ini" "$PERMISSION_BASE/config.ini"
for script in zt-globalctl post-fs-data.sh service.sh uninstall.sh; do
  printf '#!/system/bin/sh\nexit 0\n' > "$PERMISSION_MOD/$script"
done
printf 'daemon\n' > "$PERMISSION_MOD/bin/zt-globald"
printf 'library\n' > "$PERMISSION_MOD/lib/libzt.so"
chmod 0644 "$PERMISSION_MOD/zt-globalctl" "$PERMISSION_MOD/post-fs-data.sh" \
  "$PERMISSION_MOD/service.sh" "$PERMISSION_MOD/uninstall.sh" \
  "$PERMISSION_MOD/bin/zt-globald" "$PERMISSION_MOD/lib/libzt.so"
run_installer "$PERMISSION_BASE" "$PERMISSION_MOD" >/dev/null
for executable in zt-globalctl post-fs-data.sh service.sh uninstall.sh bin/zt-globald; do
  [ -x "$PERMISSION_MOD/$executable" ] || {
    echo "$executable executable mode was not restored" >&2
    exit 1
  }
done
[ -r "$PERMISSION_MOD/lib/libzt.so" ]

echo 'install Planet default lifecycle passed'
