#!/system/bin/sh

# Check before any migration or persistent-data mutation. The shipped ELF
# binaries are AArch64; a 64-bit kernel alone does not prove 64-bit userspace.
target_arch="${ARCH:-}"
if [ -z "$target_arch" ]; then
  supported_abis="$(getprop ro.product.cpu.abilist64 2>/dev/null)"
  [ -n "$supported_abis" ] || supported_abis="$(getprop ro.product.cpu.abi 2>/dev/null)"
  case ",$supported_abis," in *,arm64-v8a,*) target_arch=arm64 ;; esac
fi
if [ "$target_arch" != arm64 ] || [ "${IS64BIT:-true}" = false ]; then
  message="Unsupported architecture: ${target_arch:-unknown}. This module requires Android arm64-v8a (64-bit userspace)."
  if command -v abort >/dev/null 2>&1; then abort "$message"; fi
  printf '%s\n' "$message" >&2
  exit 1
fi

MODDIR="${MODPATH:-${0%/*}}"
BASE="${ZT_GLOBAL_BASE:-/data/adb/zt-global}"
ACTIVE_MODDIR="${ZT_GLOBAL_OLD_MODDIR:-/data/adb/modules/zerotier-libzt-global}"

stop_legacy_pidfile() {
  pidfile="$1"
  pid="$(cat "$pidfile" 2>/dev/null)"
  case "$pid" in
    *[!0-9]*|'') ;;
    *) kill -TERM "$pid" 2>/dev/null || true ;;
  esac
  rm -f "$pidfile"
}

cleanup_legacy_name_resolution() {
  legacy_root="$MODDIR"
  if [ "$ACTIVE_MODDIR" != "$MODDIR" ] && \
    { [ -e "$ACTIVE_MODDIR/resolver.sh" ] || \
      [ -e "$ACTIVE_MODDIR/system/bin/zt-mdnsd" ] || \
      [ -e "$ACTIVE_MODDIR/system/framework/zt-resolver-bridge.jar" ]; }; then
    legacy_root="$ACTIVE_MODDIR"
  fi
  legacy_resolver="$legacy_root/resolver.sh"
  legacy_mdns="$legacy_root/system/bin/zt-mdnsd"
  legacy_jar="$legacy_root/system/framework/zt-resolver-bridge.jar"
  resolver_state="$BASE/resolver"
  original_property="$resolver_state/mdns-resolution.original"
  mdns_property=persist.device_config.netd_native.mdns_resolution
  interface_name="$(sed -n 's/^interface=//p' "$BASE/config.ini" 2>/dev/null | tail -n 1)"
  [ -n "$interface_name" ] || interface_name=zt0

  # Upgrade migration from 0.3.0 builds that shipped zt-mdnsd and a raw
  # IDnsResolver helper. The old helper is used once, if still present, to
  # remove zt0 from ResolverParamsParcel before its files are discarded.
  if [ -x "$legacy_resolver" ]; then
    "$legacy_resolver" stop >/dev/null 2>&1 || true
    "$legacy_resolver" property-disable >/dev/null 2>&1 || true
  elif [ -f "$legacy_jar" ] && command -v app_process >/dev/null 2>&1; then
    app_process -Djava.class.path="$legacy_jar" /system/bin \
      io.github.ztglobal.ResolverBridge sync "$interface_name" false \
      >/dev/null 2>&1 || true
  fi

  if [ -x "$legacy_mdns" ]; then
    "$legacy_mdns" --control stop >/dev/null 2>&1 || true
  fi
  stop_legacy_pidfile "$BASE/runtime/resolver-watch.pid"
  stop_legacy_pidfile "$BASE/runtime/mdns-launcher.pid"
  stop_legacy_pidfile "$BASE/runtime/zt-mdnsd.pid"

  # Restore the property even if the previous resolver script is already
  # missing. Do not touch it without the old module's recorded value.
  if [ -f "$original_property" ]; then
    original="$(cat "$original_property" 2>/dev/null)"
    if [ -z "$original" ] || [ "$original" = __unset__ ]; then
      if command -v resetprop >/dev/null 2>&1; then
        resetprop --delete "$mdns_property" >/dev/null 2>&1 || \
          resetprop -n "$mdns_property" "" >/dev/null 2>&1 || true
      else
        setprop "$mdns_property" "" >/dev/null 2>&1 || true
      fi
    elif command -v resetprop >/dev/null 2>&1; then
      resetprop -n "$mdns_property" "$original" >/dev/null 2>&1 || true
    else
      setprop "$mdns_property" "$original" >/dev/null 2>&1 || true
    fi
  fi

  rm -rf "$resolver_state"
  rm -f "$BASE/runtime/mdns-control.sock" \
    "$BASE/logs/mdns.log" "$BASE/logs/resolver.log"
  for module_root in "$MODDIR" "$ACTIVE_MODDIR"; do
    [ -n "$module_root" ] || continue
    rm -f "$module_root/resolver.sh" \
      "$module_root/system/bin/zt-mdnsd" \
      "$module_root/system/framework/zt-resolver-bridge.jar"
    rmdir "$module_root/system/framework" 2>/dev/null || true
  done

  if [ -f "$BASE/config.ini" ]; then
    clean_config="$BASE/runtime/config.no-name-resolution.$$"
    # Retire the old interface selector as well; the daemon always creates TAP.
    if sed '/^dns_mode=/d;/^mdns_mode=/d;/^dns_resolver_enabled=/d;/^[[:space:]]*interface_mode[[:space:]]*=/d' \
      "$BASE/config.ini" > "$clean_config"; then
      chown 0:0 "$clean_config" 2>/dev/null || true
      chmod 0600 "$clean_config"
      mv -f "$clean_config" "$BASE/config.ini"
    else
      rm -f "$clean_config"
    fi
  fi
}

mkdir -p "$BASE/state" "$BASE/logs" "$BASE/runtime"
chmod 0700 "$BASE" "$BASE/state" "$BASE/logs" "$BASE/runtime"

cleanup_legacy_name_resolution

# Android/APatch unzip implementations may normalize regular files to 0644,
# including native executables stored outside a system overlay. Restore every
# lifecycle entry point and the private daemon explicitly during installation.
chmod 0755 "$MODDIR/bin" "$MODDIR/lib" 2>/dev/null || true
chmod 0755 "$MODDIR/zt-globalctl" "$MODDIR/post-fs-data.sh" \
  "$MODDIR/service.sh" "$MODDIR/uninstall.sh" \
  "$MODDIR/bin/zt-globald" 2>/dev/null || true
chmod 0644 "$MODDIR/lib/libzt.so" 2>/dev/null || true
chown 0:0 "$MODDIR/bin/zt-globald" "$MODDIR/lib/libzt.so" 2>/dev/null || true

if [ ! -f "$BASE/config.ini" ]; then
  # A first installation must start from libzt's embedded official Planet.
  # Existing installations are detected by config.ini and keep their chosen
  # custom Planet/Roots across module upgrades.
  rm -f "$BASE/planet" "$BASE/roots" "$BASE/state/roots"
  rm -rf "$BASE/planet-transaction"
  rm -f "$BASE/planet.previous" "$BASE/runtime/planet-rollback.pending"
  rm -f "$BASE/runtime"/planet-upload-*.b64 \
    "$BASE/runtime"/planet-upload-*.size \
    "$BASE/runtime"/planet-upload-*.bin
  cp "$MODDIR/config/config.example.ini" "$BASE/config.ini"
  chmod 0600 "$BASE/config.ini"
  PLANET_MESSAGE="Default Planet: ZeroTier official (embedded in libzt)"
else
  PLANET_MESSAGE="Planet setting preserved from existing installation"
fi

ui_print "ZeroTier libzt global route module installed"
ui_print "Configure: $BASE/config.ini"
ui_print "Default mode: managed ZeroTier routes only"
ui_print "$PLANET_MESSAGE"
