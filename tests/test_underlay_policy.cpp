#include "underlay_policy.h"
#include <cassert>
#include <iostream>

int main() {
  using namespace zt_underlay;
  const std::map<int, Link> links{
    {1, {"lo", "", true, true}}, {2, {"wlan0", "", true, false}},
    {3, {"rmnet_data0", "rmnet", true, false}}, {4, {"zt0", "tun", true, false}},
    {5, {"tun0", "tun", true, false}}, {6, {"wlan1", "", true, false}},
    {7, {"renamedvpn", "wireguard", true, false}}, {8, {"v4-rmnet_data0", "", true, false}},
    {9, {"wlan2", "", false, false}}
  };
  const std::vector<PrefixSource> sources{
    {"192.168.1.114/24", 2, 0, true}, {"192.168.1.0/24", 2, 1024, true},
    {"192.168.1.0/24", 6, 1030, true}, // overlapping WLANs, system selects path
    {"2001:db8:12::4/64", 2, 1024, true}, {"fe80::123/64", 2, 0, true},
    {"10.77.3.2/32", 3, 1008, true}, {"10.77.3.1/32", 3, 1008, true},
    {"172.30.0.0/24", 4, 254, true}, {"10.0.0.0/8", 5, 100, true},
    {"10.1.0.0/16", 7, 1040, true}, {"192.0.0.4/32", 8, 1042, true},
    {"10.2.0.0/16", 9, 1043, true}, {"192.168.30.0/24", 2, 1024, false},
    {"0.0.0.0/0", 2, 1024, true}, {"::/0", 2, 1024, true},
    {"192.168.2.0/24", 2, 51820, true}, {"192.168.1.114/32", 2, 255, true},
    {"172.16.0.0/16", 99, 1024, true}
  };
  const auto prefixes = selectPrefixes(links, sources, "zt0", 51820);
  const Prefixes expected{{false,"10.77.3.1/32"},{false,"10.77.3.2/32"},
    {false,"192.168.1.0/24"},{true,"2001:db8:12::/64"},{true,"fe80::/64"}};
  assert(prefixes == expected);
  assert(canonicalPrefix("2001:db8::1/128") == "2001:db8::1/128");
  assert(canonicalPrefix("224.0.0.0/4").empty());
  assert(canonicalPrefix("ff02::fb/128").empty());
  assert(canonicalPrefix("broken").empty());
  auto changed = links;
  changed.at(2).up = false;
  changed.at(6).up = false;
  assert(selectPrefixes(changed, sources, "zt0", 51820).size() == 2);
  std::cout << "Android multitable underlay policy tests passed\n";
}
