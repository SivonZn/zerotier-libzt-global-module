#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$ROOT_DIR/.." && pwd)"
OUTPUT_DIR="$PROJECT_ROOT/output"
MODULE_DIR="$ROOT_DIR/module"
VERSION="$(sed -n 's/^version=//p' "$MODULE_DIR/module.prop" | head -1)"
PACKAGE="$OUTPUT_DIR/zerotier-libzt-global-$VERSION.zip"

RUNTIME="$ROOT_DIR/build/runtime"
[[ -x "$RUNTIME/bin/zt-globald" ]] || { echo "Run build.sh first" >&2; exit 1; }
[[ -f "$RUNTIME/lib/libzt.so" ]] || { echo "libzt.so is missing" >&2; exit 1; }
WEBUI_ROOT="$ROOT_DIR/build/webroot"
[[ -f "$WEBUI_ROOT/index.html" ]] || {
  echo "WebUI build is missing; run npm run build in webui first" >&2
  exit 1
}
source "$ROOT_DIR/tools/android-toolchain.sh"
resolve_android_toolchain
# Also strip on direct packaging, even when artifacts were supplied externally.
# Build-tree originals remain available for debugging.
strip_runtime_binaries "$RUNTIME/bin/zt-globald" "$RUNTIME/lib/libzt.so"
mkdir -p "$OUTPUT_DIR"
rm -f "$PACKAGE"

# Assemble the installable tree under build/ so packaging never writes a
# generated webroot or staging directory into the project source root.
STAGING="$ROOT_DIR/build/package"
rm -rf "$STAGING"
mkdir -p "$STAGING/webroot"
cd "$MODULE_DIR"
cp module.prop customize.sh post-fs-data.sh service.sh uninstall.sh \
  zt-globalctl "$STAGING/"
cp -R "$WEBUI_ROOT/." "$STAGING/webroot/"
cp -R config "$RUNTIME/bin" "$RUNTIME/lib" "$STAGING/"

cd "$STAGING"
zip -qr "$PACKAGE" \
  module.prop customize.sh post-fs-data.sh service.sh uninstall.sh \
  zt-globalctl webroot config bin lib
echo "$PACKAGE"
