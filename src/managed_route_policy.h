#pragma once

#include <arpa/inet.h>
#include <array>
#include <string>
#include <vector>

namespace zt_policy {

struct Prefix {
  std::array<unsigned char, 16> address{};
  unsigned int length = 0;
  bool ipv6 = false;
};

inline bool parsePrefix(const std::string& cidr, Prefix& prefix) {
  const auto slash = cidr.find('/');
  if (slash == std::string::npos || slash + 1 == cidr.size()) return false;
  prefix = Prefix{};
  const auto address = cidr.substr(0, slash);
  prefix.ipv6 = address.find(':') != std::string::npos;
  const unsigned int width = prefix.ipv6 ? 128 : 32;
  for (auto i = slash + 1; i < cidr.size(); ++i) {
    if (cidr[i] < '0' || cidr[i] > '9') return false;
    prefix.length = prefix.length * 10 + static_cast<unsigned int>(cidr[i] - '0');
    if (prefix.length > width) return false;
  }
  return inet_pton(prefix.ipv6 ? AF_INET6 : AF_INET, address.c_str(), prefix.address.data()) == 1;
}

class PrefixCoverage {
  struct Node { int children[2] = {-1, -1}; bool covered = false; };
  std::vector<Node> nodes_{Node{}};

  bool covered(int node) const {
    return node >= 0 && (nodes_[node].covered ||
      (covered(nodes_[node].children[0]) && covered(nodes_[node].children[1])));
  }

public:
  void add(const Prefix& prefix) {
    int node = 0;
    for (unsigned int bit = 0; bit < prefix.length; ++bit) {
      if (nodes_[node].covered) return;
      const auto branch = (prefix.address[bit / 8] >> (7 - bit % 8)) & 1;
      if (nodes_[node].children[branch] < 0) {
        nodes_[node].children[branch] = static_cast<int>(nodes_.size());
        nodes_.push_back(Node{});
      }
      node = nodes_[node].children[branch];
    }
    nodes_[node].covered = true;
  }

  bool coversHalf() const {
    return nodes_[0].covered || covered(nodes_[0].children[0]) || covered(nodes_[0].children[1]);
  }
};

// Include assigned-address prefixes: ip addr can create connected routes even
// without a managed route. Reject the whole snapshot before any ip mutations.
inline std::string managedRoutingRejection(const std::vector<std::string>& destinations) {
  PrefixCoverage ipv4, ipv6;
  for (const auto& cidr : destinations) {
    Prefix prefix;
    if (!parsePrefix(cidr, prefix)) return "invalid managed CIDR: " + cidr;
    if (prefix.length <= 1) return "managed /0 or /1 is prohibited: " + cidr;
    (prefix.ipv6 ? ipv6 : ipv4).add(prefix);
  }
  if (ipv4.coversHalf()) return "managed IPv4 prefixes aggregate to /1 or broader";
  if (ipv6.coversHalf()) return "managed IPv6 prefixes aggregate to /1 or broader";
  return {};
}

} // namespace zt_policy
