#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC_DIR="$ROOT_DIR/build/libzt-source"
ABI="${ANDROID_ABI:-arm64-v8a}"

source "$ROOT_DIR/tools/android-toolchain.sh"
resolve_android_toolchain

bash "$ROOT_DIR/tools/prepare-libzt.sh"
BUILD_DIR="$ROOT_DIR/build/libzt-cmake/$ABI/$(< "$SRC_DIR/.patch-key")"
[[ -f "$SRC_DIR/include/ZeroTierSockets.h" ]] || { echo "libzt source missing" >&2; exit 1; }
cmake -S "$SRC_DIR" -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-23 \
  -DZTS_NDK_ONLY=ON -DBUILD_HOST_SELFTEST=OFF -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$BUILD_DIR" --parallel "$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

LIBZT_SO="$(find "$BUILD_DIR" -name 'libzt.so' -type f | head -1)"
[[ -n "$LIBZT_SO" ]] || { echo "patched libzt.so not found" >&2; exit 1; }
mkdir -p "$ROOT_DIR/build/runtime/lib"
cp "$LIBZT_SO" "$ROOT_DIR/build/runtime/lib/libzt.so"
strip_runtime_binaries "$ROOT_DIR/build/runtime/lib/libzt.so"
echo "Built and stripped patched libzt: $ROOT_DIR/build/runtime/lib/libzt.so"
