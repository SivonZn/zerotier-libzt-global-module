#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for path in bin lib webroot webui/node_modules vendor/libzt-src/arm64-v8a; do
  [[ ! -e "$ROOT_DIR/$path" ]] || { echo "generated path outside build/: $path" >&2; exit 1; }
done
for path in runtime/bin/zt-globald runtime/lib/libzt.so webroot/index.html webui/node_modules package/webroot/index.html; do
  [[ -e "$ROOT_DIR/build/$path" ]] || { echo "missing build/$path" >&2; exit 1; }
done
echo 'build layout checks passed'
