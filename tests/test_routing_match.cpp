#include <cassert>
#include <sstream>
#include "underlay_policy.h"
#include "routing-match.inc"
int main() {
  assert(outputHasRoute("192.168.100.0/24 dev zt0 scope link\n", "192.168.100.5/24", "zt0", ""));
  assert(!outputHasRoute("192.168.100.0/24 dev zt01\n", "192.168.100.5/24", "zt0", ""));
  assert(!outputHasRoute("192.168.100.0/25 dev zt0\n", "192.168.100.5/24", "zt0", ""));
  assert(outputHasRoute("192.168.100.5 dev zt0\n", "192.168.100.5/32", "zt0", ""));
  assert(outputHasRoute("2001:db8::/64 via fe80::1 dev zt0\n", "2001:0db8::5/64", "zt0", "fe80:0:0:0:0:0:0:1"));
  assert(!outputHasRoute("192.168.100.0/24 via 172.30.0.2 dev zt0\n", "192.168.100.5/24", "zt0", "172.30.0.1"));
  assert(outputHasRule("80: from all to 192.168.100.0/24 lookup 51820\n", 80, "192.168.100.5/24", "lookup", "51820"));
  assert(!outputHasRule("180: from all to 192.168.100.0/24 lookup 51820\n", 80, "192.168.100.5/24", "lookup", "51820"));
  assert(!outputHasRule("80: from all to invalid lookup 51820\n", 80, "invalid", "lookup", "51820"));
}
