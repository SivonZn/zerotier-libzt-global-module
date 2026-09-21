#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
VERSION="$(sed -n 's/^version=//p' "$ROOT_DIR/module/module.prop" | head -1)"
ARCHIVE="${1:-$ROOT_DIR/../output/zerotier-libzt-global-$VERSION.zip}"

unzip -t "$ARCHIVE" >/dev/null
TEST_ROOT="$(mktemp -d)"
CONTENTS="$TEST_ROOT/contents"
trap 'rm -rf "$TEST_ROOT"' EXIT
unzip -Z1 "$ARCHIVE" >"$CONTENTS"
grep -qx 'module.prop' "$CONTENTS"
grep -qx 'bin/zt-globald' "$CONTENTS"
grep -qx 'lib/libzt.so' "$CONTENTS"
if grep -Eq '^(module|build|vendor|patches|tools)/' "$CONTENTS"; then
  echo 'build/source/patch/tool trees must not be packaged' >&2
  exit 1
fi
for removed in system/bin/zt-globald system/lib64/libzt.so system/bin/zt-mdnsd system/framework/zt-resolver-bridge.jar resolver.sh; do
  if grep -qx "$removed" "$CONTENTS"; then
    echo "$removed must not be included in the module package" >&2
    exit 1
  fi
done
if grep -q '^tests/' "$CONTENTS"; then
  echo 'tests must not be included in the module package' >&2
  exit 1
fi
if grep -q '^docs/' "$CONTENTS"; then
  echo 'docs must not be included in the module package' >&2
  exit 1
fi
if grep -q '^system/' "$CONTENTS"; then
  echo 'system overlay content must not be included in the module package' >&2
  exit 1
fi
if grep -qx 'action.sh' "$CONTENTS"; then
  echo 'action.sh must not be included in the module package' >&2
  exit 1
fi
source "$ROOT_DIR/tools/android-toolchain.sh"
resolve_android_toolchain
for file in bin/zt-globald lib/libzt.so; do
  unpacked="$TEST_ROOT/${file##*/}"
  unzip -p "$ARCHIVE" "$file" > "$unpacked"
  "$LLVM_READELF" --sections "$unpacked" > "$TEST_ROOT/sections"
  if grep -Eq '\.symtab|\.debug_' "$TEST_ROOT/sections"; then
    echo "$file was not stripped before packaging" >&2
    exit 1
  fi
  grep -q '\.dynsym' "$TEST_ROOT/sections"
done
"$LLVM_NM" --dynamic --defined-only "$TEST_ROOT/libzt.so" > "$TEST_ROOT/exports"
for symbol in zts_node_start zts_node_get_loaded_planet zts_net_set_frame_callback zts_net_send_frame; do
  grep -q " $symbol$" "$TEST_ROOT/exports"
done
echo 'package test passed'
