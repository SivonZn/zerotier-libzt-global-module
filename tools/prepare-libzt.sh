#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM="$ROOT_DIR/vendor/libzt-src"
PATCHES="$ROOT_DIR/patches/libzt"
DEST="$ROOT_DIR/build/libzt-source"
REVISION="$(tr -d '\r\n' < "$PATCHES/base-revision")"
[[ -f "$UPSTREAM/.git" || -d "$UPSTREAM/.git" ]] || {
  echo 'Initialize source: git submodule update --init --recursive' >&2; exit 1;
}
[[ "$(git -C "$UPSTREAM" rev-parse HEAD)" == "$REVISION" ]] || {
  echo 'libzt revision differs from patches/libzt/base-revision' >&2; exit 1;
}
SUBMODULES="$(git -C "$UPSTREAM" submodule status --recursive)"
if printf '%s\n' "$SUBMODULES" | grep -Eq '^[-+U]'; then
  echo 'Nested dependencies do not match pinned commits; run git submodule update --init --recursive' >&2; exit 1
fi
[[ -z "$(git -C "$UPSTREAM" status --porcelain --untracked-files=all --ignore-submodules=none)" ]] || {
  echo 'libzt submodule is dirty; put changes in patches/libzt instead' >&2; exit 1;
}
PATCH_FILES=()
while IFS= read -r file || [[ -n "$file" ]]; do
  [[ -z "$file" || "$file" == \#* ]] && continue
  [[ "$file" != */* && "$file" == *.patch && -f "$PATCHES/$file" ]] || {
    echo "Invalid patch entry: $file" >&2; exit 1;
  }
  PATCH_FILES+=("$PATCHES/$file")
done < "$PATCHES/series"
[[ ${#PATCH_FILES[@]} -gt 0 ]] || { echo 'Empty patch series' >&2; exit 1; }
KEY="$( { printf '%s\n%s\n' "$REVISION" "$SUBMODULES"; shasum -a 256 "$0" "$PATCHES/series" "${PATCH_FILES[@]}"; } | shasum -a 256 | awk '{print $1}')"
if [[ -f "$DEST/.patch-key" && "$(< "$DEST/.patch-key")" == "$KEY" ]]; then
  echo "$DEST"; exit 0
fi
mkdir -p "$ROOT_DIR/build"
STAGE="$(mktemp -d "$ROOT_DIR/build/libzt-source.XXXXXX")"
# Export tracked files, not working-tree caches or nested .git metadata.
git -C "$UPSTREAM" archive HEAD | tar -x -C "$STAGE"
export LIBZT_STAGE="$STAGE"
git -C "$UPSTREAM" submodule foreach --quiet --recursive \
  'mkdir -p "$LIBZT_STAGE/$displaypath" && git archive HEAD | tar -x -C "$LIBZT_STAGE/$displaypath"'
# Isolate git apply from the parent repository's prefix rules.
git -C "$STAGE" init -q
for patch in "${PATCH_FILES[@]}"; do
  git -C "$STAGE" apply --check "$patch"
  git -C "$STAGE" apply "$patch"
done
printf '%s\n' "$KEY" > "$STAGE/.patch-key"
if [[ -d "$DEST" ]]; then
  OLD="$(mktemp -d "$ROOT_DIR/build/libzt-source-previous.XXXXXX")"
  rmdir "$OLD"
  mv "$DEST" "$OLD"
fi
mv "$STAGE" "$DEST"
echo "$DEST"
