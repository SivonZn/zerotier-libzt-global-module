#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I "$ROOT_DIR/src" \
  "$ROOT_DIR/tests/test_managed_route_policy.cpp" -o "$TEST_DIR/route-policy"
"$TEST_DIR/route-policy"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I "$ROOT_DIR/src" \
  "$ROOT_DIR/tests/test_underlay_policy.cpp" -o "$TEST_DIR/underlay-policy"
"$TEST_DIR/underlay-policy"
grep -q 'route_mode != "managed"' "$ROOT_DIR/src/zt-globald.cpp"
grep -q 'lowestVpn - 2' "$ROOT_DIR/src/zt-globald.cpp"
grep -q 'rule add pref' "$ROOT_DIR/src/zt-globald.cpp"
grep -q 'queryRoutingSnapshot' "$ROOT_DIR/src/zt-globald.cpp"
grep -q 'snapshotFingerprint' "$ROOT_DIR/src/zt-globald.cpp"
grep -q 'g_route_sync_ok = ok' "$ROOT_DIR/src/zt-globald.cpp"
! grep -q 'installMdnsRoutes' "$ROOT_DIR/src/zt-globald.cpp"
! grep -q 'kMdnsSocketMark' "$ROOT_DIR/src/zt-globald.cpp"
echo 'route manager policy checks passed'
