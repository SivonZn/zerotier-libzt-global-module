#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
mkdir -p "$ROOT/build/tests"
bash "$ROOT/tools/prepare-libzt.sh"
# Compile the actual patched dispatch functions with minimal protocol stubs.
# No lwIP RX declaration exists: any accidental fallback fails compilation.
awk '/^static Mutex zts_external_frame_mutex;/{p=1} /^extern Events\*/{p=0} p;
 /^void VirtualTap::put\(/{q=1} /^void VirtualTap::scanMulticastGroups\(/{q=0} q;
 /^signed char zts_lwip_eth_tx\(/{r=1} /^void zts_lwip_eth_rx\(/{r=0} r' \
 "$ROOT/build/libzt-source/src/VirtualTap.cpp" > "$ROOT/build/tests/frame-dispatch.inc"
awk '/^bool outputHasRule\(/{p=1} /^bool applyOwnedEntry\(/{p=0} p;
 /^bool outputHasRoute\(/{q=1} /^bool verifyRoutingState\(/{q=0} q' \
 "$ROOT/src/zt-globald.cpp" > "$ROOT/build/tests/routing-match.inc"
for test in frame_dispatch routing_match; do
  "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pthread \
    -I "$ROOT/build/tests" -I "$ROOT/src" "$ROOT/tests/test_$test.cpp" \
    -o "$ROOT/build/tests/$test"
  "$ROOT/build/tests/$test"
done
echo 'Frame dispatch and routing prefix regressions passed'
