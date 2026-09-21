#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$ROOT_DIR/build/webui"
mkdir -p "$WORK"
# Source mirrors and all Node dependencies/cache live under build/. --delete
# applies only to this generated source subtree, never to the source itself.
mkdir -p "$WORK/src"
rsync -a --delete "$ROOT_DIR/webui/src/" "$WORK/src/"
cp "$ROOT_DIR/webui/"{package.json,package-lock.json,tsconfig.json,vite.config.ts,index.html} "$WORK/"
if [[ ! -d "$WORK/node_modules" ]] || ! cmp -s "$WORK/package-lock.json" "$WORK/.installed-lock"; then
  # Use the lockfile without inheriting machine-private npm script policies.
  (cd "$WORK" && npm ci --userconfig=/dev/null --cache "$ROOT_DIR/build/npm-cache" --no-audit --no-fund)
  cp "$WORK/package-lock.json" "$WORK/.installed-lock"
fi
cd "$WORK"
case "${1:-build}" in
  build) exec node node_modules/vite/bin/vite.js build ;;
  typecheck) exec node node_modules/typescript/bin/tsc --noEmit ;;
  dev)
    (while sleep 1; do rsync -a --delete "$ROOT_DIR/webui/src/" "$WORK/src/"; done) &
    watcher=$!
    trap 'kill "$watcher" 2>/dev/null || true' EXIT
    node node_modules/vite/bin/vite.js
    ;;
  *) echo 'usage: webui.sh build|typecheck|dev' >&2; exit 2 ;;
esac
