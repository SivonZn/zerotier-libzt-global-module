#!/system/bin/sh

BASE=/data/adb/zt-global
MODDIR="${0%/*}"
mkdir -p "$BASE/state" "$BASE/logs" "$BASE/runtime"
chmod 0700 "$BASE" "$BASE/state" "$BASE/logs" "$BASE/runtime"

if [ -f "$MODDIR/bin/zt-globald" ]; then
  chown 0:0 "$MODDIR/bin/zt-globald"
  chmod 0755 "$MODDIR/bin/zt-globald"
fi
