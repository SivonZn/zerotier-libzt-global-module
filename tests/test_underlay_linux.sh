#!/usr/bin/env bash
# Run ONLY in an isolated, disposable Linux network namespace/container with
# CAP_NET_ADMIN and g++/iproute2. Never run on a phone or the host namespace.
set -euo pipefail
[[ "${ZT_TEST_ISOLATED_NETNS:-}" == 1 ]] || { echo 'isolated test namespace required' >&2; exit 2; }
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_DIR"' EXIT
g++ -std=c++17 -O1 -Wall -Wextra -Werror -Wno-unused-result -ffunction-sections -fdata-sections \
  -I "$ROOT_DIR/src" -I "$ROOT_DIR/build/libzt-source/include" \
  "$ROOT_DIR/tests/test_underlay_linux.cpp" -Wl,--gc-sections -pthread -o "$TEST_DIR/test"
mkdir -p /data/adb/zt-global/runtime
for dev in wlan0 wlan1 rmnet_data0; do
  ip link add "$dev" type bridge
  ip link set "$dev" up
done
for dev in zt0 privatevpn; do
  ip link add "$dev" type dummy
  ip link set "$dev" up
done
ip addr add 192.168.50.2/24 dev wlan0 noprefixroute
ip addr add 192.168.50.3/24 dev wlan1 noprefixroute
ip -6 addr add 2001:db8:50::2/64 dev wlan0 nodad noprefixroute
ip addr add 10.77.3.2/32 dev rmnet_data0 noprefixroute
ip addr add 10.99.0.1/16 dev privatevpn noprefixroute
ip route add 192.168.50.0/24 dev wlan0 table 1001
ip route add 192.168.50.0/24 dev wlan1 table 1002
ip -6 route add 2001:db8:50::/64 dev wlan0 table 1001
ip route add 10.77.3.1/32 dev rmnet_data0 table 1008
ip rule add pref 100 fwmark 2 lookup 1002
ip rule add pref 110 to 192.168.50.0/24 lookup 1001
ip -6 rule add pref 110 to 2001:db8:50::/64 lookup 1001
# Allow autoconfig/link events to settle before requesting a coherent dump.
sleep 2
"$TEST_DIR/test" parser
"$TEST_DIR/test" install
ip route get 192.168.50.99 | grep -q 'dev wlan0'
ip route get 192.168.50.99 mark 2 | grep -q 'dev wlan1'
ip -6 route get 2001:db8:50::99 | grep -q 'dev wlan0'
ip route get 192.168.60.99 | grep -q 'dev zt0'
! ip route show table main | grep -q 'dev zt0'
! ip -6 route show table main | grep -q '^fd88::/64'
# A missed physical-table route must NOT fall through to overlay priority 80.
ip route del 192.168.50.0/24 dev wlan0 table 1001
! ip route get 192.168.50.99 | grep -q 'dev zt0'
ip route add 192.168.50.0/24 dev wlan0 table 1001
# Simulate an external rule flush, verify it is detected and reinstallable.
ip rule del pref 81
"$TEST_DIR/test" missing-anchor
ip route get 192.168.50.99 | grep -q 'dev wlan0'
"$TEST_DIR/test" cleanup
! ip rule show | grep -Eq '^(79|80|81):'
! ip -6 rule show | grep -Eq '^(79|80|81):'
ip rule show | grep -q '^100:'
ip rule show | grep -q '^110:'
ip route show table 1001 | grep -q 'dev wlan0'
"$TEST_DIR/test" fail-install
ip link set wlan0 down
ip link set wlan1 down
"$TEST_DIR/test" down
echo 'underlay Linux multitable / fwmark / IPv6 / failure / cleanup tests passed'
