#!/system/bin/sh

MODDIR="${0%/*}"
BASE=/data/adb/zt-global
DAEMON="$MODDIR/bin/zt-globald"
CONFIG="$BASE/config.ini"
LOG="$BASE/logs/daemon.log"
PIDFILE="$BASE/runtime/zt-globald.pid"
START_LOCK="$BASE/runtime/service-start.lock"
export LD_LIBRARY_PATH="$MODDIR/lib:${LD_LIBRARY_PATH:-}"

# Repair permissions before testing executability. APatch may extract a
# private top-level native binary as 0644 even when the ZIP records 0755.
if [ -f "$DAEMON" ]; then
  chown 0:0 "$DAEMON" 2>/dev/null || true
  chmod 0755 "$DAEMON" 2>/dev/null || true
fi

until [ "$(getprop sys.boot_completed)" = "1" ]; do
  sleep 2
done

[ -n "${ZT_GLOBAL_ACTIVATION_ID:-}" ] || sleep 3
mkdir -p "$BASE/logs" "$BASE/runtime"
chmod 0700 "$BASE/logs" "$BASE/runtime"

# Resume an interrupted Planet transaction on boot, before taking the service
# start lock. Starts initiated by the transaction itself bypass this path.
if [ -d "$BASE/planet-transaction" ] && [ -z "${ZT_GLOBAL_ACTIVATION_ID:-}" ]; then
  exec sh "$MODDIR/zt-globalctl" restart
fi

daemon_pid_alive() {
  pid="$(cat "$PIDFILE" 2>/dev/null)"
  case "$pid" in *[!0-9]*|'') return 1 ;; esac
  kill -0 "$pid" 2>/dev/null || return 1
  executable="$(readlink "/proc/$pid/exe" 2>/dev/null)"
  [ "$executable" = "$DAEMON" ] || [ "$executable" = "$DAEMON (deleted)" ]
}

# Serialize parallel service invocations. The native daemon also owns a
# non-blocking flock, so a stale or raced shell lock cannot create two nodes.
if ! mkdir "$START_LOCK" 2>/dev/null; then
  lock_pid="$(cat "$START_LOCK/pid" 2>/dev/null)"
  case "$lock_pid" in
    *[!0-9]*|'') rm -rf "$START_LOCK" 2>/dev/null || exit 0 ;;
    *) lock_command="$(tr '\000' ' ' < "/proc/$lock_pid/cmdline" 2>/dev/null)"
       if kill -0 "$lock_pid" 2>/dev/null; then
         case "$lock_command" in *"$MODDIR/service.sh"*) exit 0 ;; esac
       fi
       rm -rf "$START_LOCK" 2>/dev/null || exit 0 ;;
  esac
  mkdir "$START_LOCK" 2>/dev/null || exit 0
fi
echo $$ > "$START_LOCK/pid"
trap 'rm -rf "$START_LOCK"' EXIT HUP INT TERM

if daemon_pid_alive || "$DAEMON" --control status >/dev/null 2>&1; then
  exit 0
fi
rm -f "$PIDFILE" "$BASE/runtime/control.sock"

# Finish cleanup for upgrades from builds that shipped name-resolution
# helpers. This also covers managers that run customize.sh in a staging
# namespace while the old service is still exiting.
rm -f "$BASE/runtime/zt-mdnsd.pid" "$BASE/runtime/mdns-launcher.pid" \
  "$BASE/runtime/resolver-watch.pid" "$BASE/runtime/mdns-control.sock" \
  "$BASE/logs/mdns.log" "$BASE/logs/resolver.log"

if [ -x "$DAEMON" ] && [ -f "$CONFIG" ]; then
  nohup "$DAEMON" --config "$CONFIG" \
    --activation-id "${ZT_GLOBAL_ACTIVATION_ID:-}" \
    >>"$LOG" 2>&1 </dev/null &
  child=$!
  sleep 1
  kill -0 "$child" 2>/dev/null || exit 1
else
  exit 1
fi
