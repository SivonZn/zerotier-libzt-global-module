#!/system/bin/sh

BASE=/data/adb/zt-global
MODDIR="${0%/*}"

if [ -f "$MODDIR/zt-globalctl" ]; then
  ZT_GLOBAL_BASE="$BASE" sh "$MODDIR/zt-globalctl" stop >/dev/null 2>&1 || true
fi

rm -f "$BASE/runtime/zt-globald.pid" "$BASE/runtime/control.sock"
