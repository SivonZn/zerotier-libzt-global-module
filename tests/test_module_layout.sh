#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for entry in module.prop customize.sh post-fs-data.sh service.sh uninstall.sh zt-globalctl config; do
  [[ -e "$ROOT_DIR/module/$entry" ]]
  [[ ! -e "$ROOT_DIR/$entry" ]]
done
for script in build_libzt.sh package.sh; do
  [[ ! -e "$ROOT_DIR/$script" ]]
  [[ -x "$ROOT_DIR/scripts/$script" ]]
  bash -n "$ROOT_DIR/scripts/$script"
done
echo 'module source layout checks passed'
