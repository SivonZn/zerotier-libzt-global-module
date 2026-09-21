#!/usr/bin/env bash
set -euo pipefail
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TEST_ROOT="$(mktemp -d)"
trap 'rm -rf "$TEST_ROOT"' EXIT
mkdir "$TEST_ROOT/bin"
printf '#!/bin/sh\nexit 0\n' > "$TEST_ROOT/bin/chown"
chmod +x "$TEST_ROOT/bin/chown"
export PATH="$TEST_ROOT/bin:$PATH"
export ZT_GLOBAL_BASE="$TEST_ROOT"
export ZT_GLOBAL_CTL_LOCKED=1 # Android native flock is exercised on-device.
cp "$ROOT_DIR/module/config/config.example.ini" "$TEST_ROOT/config.ini"
# Direct upgrades that bypass the installer must not expose the obsolete mode.
printf 'interface_mode=tun\n' >> "$TEST_ROOT/config.ini"
config="$(sh "$ROOT_DIR/module/zt-globalctl" config)"
[[ "$config" != *interfaceMode* ]]
[[ "$config" == *'"routingTable":51820'* ]]
result="$(sh "$ROOT_DIR/module/zt-globalctl" save true 0123456789abcdef 80 51821 1350 9993)"
[[ "$result" == '{"ok":true}' ]]
! grep -q interface_mode "$TEST_ROOT/config.ini"
for pair in network_id=0123456789abcdef rule_priority=80 routing_table=51821 mtu=1350 port=9993; do
  grep -qx "$pair" "$TEST_ROOT/config.ini"
done
cp "$TEST_ROOT/config.ini" "$TEST_ROOT/config.before"
# Reject the old positional API rather than shifting mode into priority.
if sh "$ROOT_DIR/module/zt-globalctl" save true 0123456789abcdef tun 80 51821 1350 9993; then
  echo 'obsolete mode argument was accepted' >&2
  exit 1
fi
cmp "$TEST_ROOT/config.before" "$TEST_ROOT/config.ini"
# Native Ethernet bridge must unconditionally request TAP.
grep -Fq 'request.ifr_flags = IFF_TAP | IFF_NO_PI;' "$ROOT_DIR/src/zt-globald.cpp"
! grep -Eq 'IFF_TUN|interface_mode' "$ROOT_DIR/src/zt-globald.cpp"
! grep -R -E 'interfaceMode|SelectField|TUN（' "$ROOT_DIR/webui/src"
echo 'TAP-only configuration and bridge checks passed'
