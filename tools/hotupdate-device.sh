#!/system/bin/sh
set -eu
MOD=/data/adb/modules/zerotier-libzt-global
BASE=/data/adb/zt-global
ZIP="$1"
WORK="$(mktemp -d "$BASE/runtime/hotupdate.XXXXXX")"
chmod 700 "$WORK"
mkdir "$WORK/previous" "$WORK/new"
cp -a "$MOD/." "$WORK/previous/"
unzip -o "$ZIP" -d "$WORK/new" >/dev/null
sha256sum "$BASE/state/identity.public" > "$WORK/identity-before"
if [ -f "$BASE/planet" ]; then sha256sum "$BASE/planet" >> "$WORK/identity-before"; fi
if "$MOD/zt-globalctl" status | grep -q '"running":true'; then
  "$MOD/zt-globalctl" stop
fi
for item in bin lib webroot; do
  mv "$MOD/$item" "$WORK/old-$item"
  cp -a "$WORK/new/$item" "$MOD/$item"
done
for item in module.prop service.sh post-fs-data.sh uninstall.sh zt-globalctl; do
  # Rename, never truncate a script another status request may be reading.
  cp -p "$WORK/new/$item" "$MOD/$item.update"
  mv "$MOD/$item.update" "$MOD/$item"
done
chmod 755 "$MOD/bin/zt-globald" "$MOD/service.sh" "$MOD/post-fs-data.sh" "$MOD/uninstall.sh" "$MOD/zt-globalctl"
chmod 644 "$MOD/lib/libzt.so"
if ! sh "$MOD/service.sh"; then
  echo "start failed; restoring runtime backup $WORK"
  cp -a "$WORK/previous/." "$MOD/"
  sh "$MOD/service.sh"
  exit 1
fi
sha256sum -c "$WORK/identity-before"
echo "Runtime backup: $WORK"
"$MOD/zt-globalctl" status
