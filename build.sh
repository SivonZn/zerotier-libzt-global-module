#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ABI="${ANDROID_ABI:-arm64-v8a}"

source "$ROOT_DIR/tools/android-toolchain.sh"
resolve_android_toolchain

# The patched libzt source and artifact are module-local. This avoids relying
# on another private checkout or a machine-specific absolute path.
LIBZT_ROOT="$ROOT_DIR/build/libzt-sdk/$ABI"
# Always validate/reapply patch inputs and incrementally build libzt, so a
# changed patch cannot silently pair a new header with an old library.
"$ROOT_DIR/scripts/build_libzt.sh"
if [[ -f "$ROOT_DIR/build/runtime/lib/libzt.so" && -f "$ROOT_DIR/build/libzt-source/include/ZeroTierSockets.h" ]]; then
  mkdir -p "$LIBZT_ROOT/include" "$LIBZT_ROOT/lib"
  cp "$ROOT_DIR/build/libzt-source/include/ZeroTierSockets.h" "$LIBZT_ROOT/include/ZeroTierSockets.h"
  cp "$ROOT_DIR/build/runtime/lib/libzt.so" "$LIBZT_ROOT/lib/libzt.so"
fi

if [[ ! -f "$LIBZT_ROOT/lib/libzt.so" || ! -f "$LIBZT_ROOT/include/ZeroTierSockets.h" ]]; then
  echo "libzt artifact not found under $LIBZT_ROOT" >&2
  exit 1
fi

BUILD_DIR="$ROOT_DIR/build/$ABI"
rm -rf "$BUILD_DIR"
cmake -S "$ROOT_DIR/src" -B "$BUILD_DIR" \
  -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI="$ABI" -DANDROID_PLATFORM=android-24 \
  -DLIBZT_ROOT="$LIBZT_ROOT" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" --parallel "$(sysctl -n hw.ncpu 2>/dev/null || nproc)"

mkdir -p "$ROOT_DIR/build/runtime/bin" "$ROOT_DIR/build/runtime/lib"
cp "$BUILD_DIR/zt-globald" "$ROOT_DIR/build/runtime/bin/zt-globald"
cp "$LIBZT_ROOT/lib/libzt.so" "$ROOT_DIR/build/runtime/lib/libzt.so"
strip_runtime_binaries "$ROOT_DIR/build/runtime/bin/zt-globald" "$ROOT_DIR/build/runtime/lib/libzt.so"
chmod 0755 "$ROOT_DIR/build/runtime/bin/zt-globald"
echo "Built and stripped $ABI zt-globald and libzt.so"
