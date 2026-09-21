#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for entry in module.prop customize.sh post-fs-data.sh service.sh uninstall.sh zt-globalctl config; do
  [[ -e "$ROOT_DIR/module/$entry" ]]
  [[ ! -e "$ROOT_DIR/$entry" ]]
done
bash -n "$ROOT_DIR/package.sh"
echo 'module source layout checks passed'
