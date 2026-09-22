#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bash "$ROOT_DIR/tools/prepare-libzt.sh"
SRC="$ROOT_DIR/build/libzt-source"
key="$(< "$SRC/.patch-key")"
bash "$ROOT_DIR/tools/prepare-libzt.sh"
[[ "$(< "$SRC/.patch-key")" == "$key" ]]
[[ -z "$(git -C "$ROOT_DIR/vendor/libzt-src" status --porcelain --untracked-files=all)" ]]
grep -q 'zts_node_get_loaded_planet' "$SRC/include/ZeroTierSockets.h"
grep -q 'zts_net_set_frame_callback' "$SRC/include/ZeroTierSockets.h"
grep -q 'zts_net_multicast_add' "$SRC/include/ZeroTierSockets.h"
! grep -q 'zts_node_get_loaded_planet' "$ROOT_DIR/vendor/libzt-src/include/ZeroTierSockets.h"
# Check overlapping patches sequentially in a disposable generated copy.
CHECK="$(mktemp -d "$ROOT_DIR/build/patch-check.XXXXXX")"
cp -R "$SRC/src" "$SRC/include" "$SRC/CMakeLists.txt" "$CHECK/"
mkdir -p "$CHECK/ext/ZeroTierOne/node" "$CHECK/ext/ZeroTierOne/ext"
cp "$SRC/ext/ZeroTierOne/node/C25519.hpp" "$CHECK/ext/ZeroTierOne/node/"
cp -R "$SRC/ext/ZeroTierOne/ext/prometheus-cpp-lite-1.0" "$CHECK/ext/ZeroTierOne/ext/"
git -C "$CHECK" init -q
for patch in 0005-gcc-prometheus-stdexcept.patch 0004-identity-key-validation.patch 0003-exclusive-external-tap.patch 0002-tap-routing-multicast-planet.patch 0001-android-native-only.patch; do
  git -C "$CHECK" apply --reverse --check "$ROOT_DIR/patches/libzt/$patch"
  git -C "$CHECK" apply --reverse "$ROOT_DIR/patches/libzt/$patch"
done
echo 'libzt pinned submodule / patch idempotence checks passed'
