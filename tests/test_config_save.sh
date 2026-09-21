#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_ROOT="$(mktemp -d)"
trap 'rm -rf "$TEST_ROOT"' EXIT
mkdir "$TEST_ROOT/bin"
printf '#!/bin/sh\nexit "${MOCK_CHOWN_RESULT:-0}"\n' > "$TEST_ROOT/bin/chown"
chmod +x "$TEST_ROOT/bin/chown"
export PATH="$TEST_ROOT/bin:$PATH" ZT_GLOBAL_BASE="$TEST_ROOT/data" ZT_GLOBAL_CTL_LOCKED=1
mkdir "$ZT_GLOBAL_BASE"
ctl() { sh "$ROOT_DIR/module/zt-globalctl" "$@"; }
cp "$ROOT_DIR/tests/fixtures/config-preserve.ini" "$ZT_GLOBAL_BASE/config.ini"
config="$(ctl config)"
[[ "$config" == *'"networkId":"bbbbbbbbbbbbbbbb"'* ]]
[[ "$config" == *'"mtu":1450'* ]]
ctl save false 0123456789ABCDEF 85 51822 1380 9994 >/dev/null
for field in enabled=false network_id=0123456789abcdef rule_priority=85 routing_table=51822 mtu=1380 port=9994; do
  grep -qx "$field" "$ZT_GLOBAL_BASE/config.ini"
  [ "$(grep -c "^${field%%=*}=" "$ZT_GLOBAL_BASE/config.ini")" = 1 ]
done
strip_owned_keys() {
  sed -E '/^[[:space:]]*(enabled|network_id|rule_priority|routing_table|mtu|port|interface_mode)[[:space:]]*=/d' "$1"
}
diff -u <(strip_owned_keys "$ROOT_DIR/tests/fixtures/config-preserve.ini") <(strip_owned_keys "$ZT_GLOBAL_BASE/config.ini")
cp "$ZT_GLOBAL_BASE/config.ini" "$TEST_ROOT/saved"
ctl save false 0123456789abcdef 85 51822 1380 9994 >/dev/null
cmp "$TEST_ROOT/saved" "$ZT_GLOBAL_BASE/config.ini"
if ctl save true bad-network-id auto 51820 1400 0 >/dev/null; then exit 1; fi
cmp "$TEST_ROOT/saved" "$ZT_GLOBAL_BASE/config.ini"
for invalid_priority in 0 1 32765 32766; do
  if ctl save true 0123456789abcdef "$invalid_priority" 51820 1400 0 >/dev/null; then exit 1; fi
  cmp "$TEST_ROOT/saved" "$ZT_GLOBAL_BASE/config.ini"
done
if MOCK_CHOWN_RESULT=1 ctl save true 0123456789abcdef auto 51820 1400 0 >/dev/null; then exit 1; fi
cmp "$TEST_ROOT/saved" "$ZT_GLOBAL_BASE/config.ini"
# Missing UI keys are appended; unrelated fields survive.
printf '# sparse\ncustom_key=keep\n' > "$ZT_GLOBAL_BASE/config.ini"
ctl save true '' auto 51820 1400 0 >/dev/null
grep -qx 'custom_key=keep' "$ZT_GLOBAL_BASE/config.ini"
grep -qx 'network_id=' "$ZT_GLOBAL_BASE/config.ini"
# A first save uses the installation template for non-UI defaults.
export ZT_GLOBAL_BASE="$TEST_ROOT/fresh"
ctl save true 0123456789abcdef auto 51820 1400 0 >/dev/null
grep -qx 'interface=zt0' "$ZT_GLOBAL_BASE/config.ini"
grep -qx 'storage_path=/data/adb/zt-global/state' "$ZT_GLOBAL_BASE/config.ini"
echo 'field-level atomic config save tests passed'
