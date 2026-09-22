#pragma once

#include "managed_route_policy.h"
#include <algorithm>
#include <map>
#include <utility>

namespace zt_underlay {

struct Link {
  std::string name, kind;
  bool up = false, loopback = false;
};
struct PrefixSource {
  std::string cidr;
  int interface_index = 0;
  unsigned int table = 0;
  bool direct = true;
};
using Prefixes = std::vector<std::pair<bool, std::string>>;

inline bool eligible(const Link& link, const std::string& overlay) {
  if (!link.up || link.loopback || link.name.empty() || link.name == overlay) return false;
  for (const char* prefix : {"tun", "tap", "dummy", "vpn", "vti", "wg", "clat", "ipsec", "zt", "v4-", "v6-"})
    if (link.name.rfind(prefix, 0) == 0) return false;
  // Android physical links normally have no kind or use rmnet. Unknown kinds
  // fail closed: a renamed tunnel must not suppress managed destinations.
  return link.kind.empty() || link.kind == "rmnet" || link.kind == "vlan" ||
         link.kind == "bridge" || link.kind == "bond";
}

inline std::string canonicalNetworkPrefix(const std::string& cidr) {
  zt_policy::Prefix prefix;
  if (!zt_policy::parsePrefix(cidr, prefix)) return {};
  const auto width = prefix.ipv6 ? 128u : 32u;
  for (auto bit = prefix.length; bit < width; ++bit)
    prefix.address[bit / 8] &= static_cast<unsigned char>(~(1u << (7 - bit % 8)));
  char buffer[INET6_ADDRSTRLEN]{};
  if (!inet_ntop(prefix.ipv6 ? AF_INET6 : AF_INET, prefix.address.data(), buffer, sizeof(buffer))) return {};
  return std::string(buffer) + "/" + std::to_string(prefix.length);
}

inline std::string canonicalPrefix(const std::string& cidr) {
  const auto canonical = canonicalNetworkPrefix(cidr);
  zt_policy::Prefix prefix;
  if (!zt_policy::parsePrefix(canonical, prefix) || prefix.length <= 1) return {};
  // Physical LAN filtering must not affect general managed-route comparison.
  if ((!prefix.ipv6 && (prefix.address[0] == 0 || prefix.address[0] >= 224)) ||
      (prefix.ipv6 && prefix.address[0] == 0xff)) return {};
  return canonical;
}

inline Prefixes selectPrefixes(const std::map<int, Link>& links,
                              const std::vector<PrefixSource>& sources,
                              const std::string& overlay, unsigned int overlayTable) {
  Prefixes result;
  for (const auto& source : sources) {
    const auto link = links.find(source.interface_index);
    if (!source.direct || source.table == overlayTable || source.table == 255 ||
        link == links.end() || !eligible(link->second, overlay)) continue;
    const auto cidr = canonicalPrefix(source.cidr);
    if (!cidr.empty()) result.emplace_back(cidr.find(':') != std::string::npos, cidr);
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

} // namespace zt_underlay
