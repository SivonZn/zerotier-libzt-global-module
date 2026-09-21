#include "managed_route_policy.h"
#include <cassert>
#include <iostream>

int main() {
  using zt_policy::managedRoutingRejection;
  const auto allowed = [](std::vector<std::string> routes) {
    assert(managedRoutingRejection(routes).empty());
  };
  const auto blocked = [](std::vector<std::string> routes) {
    assert(!managedRoutingRejection(routes).empty());
  };
  allowed({});
  allowed({"172.30.0.135/24", "172.30.0.0/24", "192.168.1.0/24", "192.168.30.0/24"});
  allowed({"10.0.0.0/8", "172.16.0.0/12", "192.168.0.0/16", "fd00::123/64", "2001:db8::/32"});
  allowed({"1.1.1.1/32", "2606:4700:4700::1111/128"});
  blocked({"0.0.0.0/0"});
  blocked({"0.0.0.0/1", "128.0.0.0/1"});
  blocked({"128.0.0.0/1"});
  blocked({"192.168.1.3/1"}); // Noncanonical address must not bypass the check.
  blocked({"::/0"});
  blocked({"::/1", "8000::/1"});
  blocked({"8000::/01"});
  blocked({"0.0.0.0/2", "64.0.0.0/2", "128.0.0.0/2", "192.0.0.0/2"});
  blocked({"0.0.0.0/3", "32.0.0.0/3", "64.0.0.0/2"});
  blocked({"::/2", "4000::/3", "6000::/3"});
  // Duplicates and overlapping prefixes do not double-count coverage.
  allowed({"0.0.0.0/2", "0.0.0.0/2", "0.0.0.0/3", "128.0.0.0/2"});
  allowed({"0.0.0.0/2", "4000::/2"}); // Families must be evaluated separately.
  for (const auto& malformed : {"default", "0.0.0.0", "0.0.0.0/33", "::/129",
       "0.0.0.0/-1", "0.0.0.0/", "garbage/24", "::/64;reboot", "1.2.3.4/999999999999999999999"})
    blocked({malformed});
  std::cout << "managed route policy tests passed\n";
}
