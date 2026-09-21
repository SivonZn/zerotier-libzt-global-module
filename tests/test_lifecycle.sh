#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
for script in customize.sh post-fs-data.sh service.sh uninstall.sh zt-globalctl; do
  sh -n "$ROOT_DIR/module/$script"
done
[ ! -e "$ROOT_DIR/module/action.sh" ]
[ ! -e "$ROOT_DIR/module/resolver.sh" ]
[ ! -e "$ROOT_DIR/build_resolver_bridge.sh" ]
[ ! -e "$ROOT_DIR/src/zt-mdnsd.cpp" ]
[ ! -e "$ROOT_DIR/src/java/ResolverBridge.java" ]
! grep -q 'installMdnsRoutes' "$ROOT_DIR/src/zt-globald.cpp"
! grep -q 'mdns-status' "$ROOT_DIR/module/zt-globalctl"
! grep -q 'dns_resolver_enabled' "$ROOT_DIR/module/zt-globalctl"
! grep -q 'dns_resolver_enabled' "$ROOT_DIR/module/config/config.example.ini"
grep -q 'daemon_pids()' "$ROOT_DIR/module/zt-globalctl"
grep -q 'flock(g_instance_lock_fd, LOCK_EX | LOCK_NB)' "$ROOT_DIR/src/zt-globald.cpp"
grep -q 'service-start.lock' "$ROOT_DIR/module/service.sh"
grep -q 'dataPlaneReady' "$ROOT_DIR/src/zt-globald.cpp"
grep -q 'synchronizeRouting(config, priority' "$ROOT_DIR/src/zt-globald.cpp"
grep -q 'verifyRoutingState' "$ROOT_DIR/src/zt-globald.cpp"
grep -q 'DAEMON="$MODDIR/bin/zt-globald"' "$ROOT_DIR/module/service.sh"
grep -q 'LD_LIBRARY_PATH="$MODDIR/lib:' "$ROOT_DIR/module/service.sh"
grep -q '\[ -f "$MODDIR/bin/zt-globald" \]' "$ROOT_DIR/module/post-fs-data.sh"
grep -q '\[ -f "$DAEMON" \]' "$ROOT_DIR/module/service.sh"
[ ! -d "$ROOT_DIR/module/system" ]
grep -q 'planet-reset' "$ROOT_DIR/module/zt-globalctl"
grep -q 'mode":"official' "$ROOT_DIR/module/zt-globalctl"
bash "$ROOT_DIR/tests/test_planet_reset.sh"
bash "$ROOT_DIR/tests/test_install_planet_default.sh"
echo 'lifecycle shell syntax passed'
