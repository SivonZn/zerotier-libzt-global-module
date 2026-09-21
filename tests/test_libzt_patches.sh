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
# A reverse check proves the complete ordered series is present exactly once.
git -C "$SRC" apply --reverse --check \
  "$ROOT_DIR/patches/libzt/0002-tap-routing-multicast-planet.patch" \
  "$ROOT_DIR/patches/libzt/0001-android-native-only.patch"
echo 'libzt pinned submodule / patch idempotence checks passed'
