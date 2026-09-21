#pragma once

#include "underlay_policy.h"
#include <linux/if_addr.h>
#include <linux/if_link.h>
#include <linux/fib_rules.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <chrono>
#include <cerrno>
#include <cstring>

namespace zt_underlay {

// Native RPDB actions avoid depending on Android iproute2's support for `nop`.
// goto(P+1) + an explicit NOP anchor skips ONLY our P-priority overlay rules.
// netd's fwmark/UID/OIF selection resumes unchanged after the anchor.
inline bool changeBypassRule(bool ipv6, unsigned int priority, const std::string& target,
                             unsigned int jump, bool install) {
  const int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_ROUTE);
  if (fd < 0) return false;
  struct Close { int fd; ~Close() { close(fd); } } closer{fd};
  struct { nlmsghdr header; fib_rule_hdr rule; char attributes[128]; } request{};
  request.header.nlmsg_len = NLMSG_LENGTH(sizeof(fib_rule_hdr));
  request.header.nlmsg_type = install ? RTM_NEWRULE : RTM_DELRULE;
  request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_ACK | (install ? NLM_F_CREATE | NLM_F_EXCL : 0);
  request.header.nlmsg_seq = 1;
  request.rule.family = ipv6 ? AF_INET6 : AF_INET;
  request.rule.action = jump ? FR_ACT_GOTO : FR_ACT_NOP;
  auto add = [&](int type, const void* value, size_t length) {
    auto* attr = reinterpret_cast<rtattr*>(reinterpret_cast<char*>(&request) + NLMSG_ALIGN(request.header.nlmsg_len));
    attr->rta_type = type;
    attr->rta_len = RTA_LENGTH(length);
    std::memcpy(RTA_DATA(attr), value, length);
    request.header.nlmsg_len = NLMSG_ALIGN(request.header.nlmsg_len) + RTA_ALIGN(attr->rta_len);
  };
  add(FRA_PRIORITY, &priority, sizeof(priority));
  if (jump) {
    zt_policy::Prefix prefix;
    if (!zt_policy::parsePrefix(target, prefix) || prefix.ipv6 != ipv6) return false;
    request.rule.dst_len = prefix.length;
    add(FRA_DST, prefix.address.data(), ipv6 ? 16 : 4);
    add(FRA_GOTO, &jump, sizeof(jump));
  }
  sockaddr_nl kernel{};
  kernel.nl_family = AF_NETLINK;
  if (sendto(fd, &request, request.header.nlmsg_len, 0, reinterpret_cast<sockaddr*>(&kernel), sizeof(kernel)) < 0) return false;
  pollfd descriptor{fd, POLLIN, 0};
  if (poll(&descriptor, 1, 2000) <= 0) return false;
  alignas(nlmsghdr) char buffer[4096];
  const auto size = recv(fd, buffer, sizeof(buffer), 0);
  if (size < static_cast<ssize_t>(NLMSG_LENGTH(sizeof(nlmsgerr)))) return false;
  const auto* header = reinterpret_cast<const nlmsghdr*>(buffer);
  if (header->nlmsg_type != NLMSG_ERROR || header->nlmsg_seq != 1) return false;
  const auto* result = static_cast<const nlmsgerr*>(NLMSG_DATA(header));
  return result->error == 0 || (!install && result->error == -ENOENT);
}

template<typename Callback>
inline bool attributes(const void* data, int size, Callback callback) {
  auto* attr = reinterpret_cast<const rtattr*>(data);
  while (RTA_OK(attr, size)) {
    if (!callback(attr->rta_type & NLA_TYPE_MASK, RTA_DATA(attr), RTA_PAYLOAD(attr))) return false;
    attr = RTA_NEXT(attr, size);
  }
  return size == 0;
}

inline bool uintAttribute(const void* data, size_t size, unsigned int& value) {
  if (size != sizeof(value)) return false;
  std::memcpy(&value, data, sizeof(value));
  return true;
}

inline bool stringAttribute(const void* data, size_t size, std::string& value) {
  const auto* text = static_cast<const char*>(data);
  if (size == 0 || !std::memchr(text, 0, size)) return false;
  value = text;
  return true;
}

inline bool decodeLink(const nlmsghdr* header, std::map<int, Link>& links) {
  if (header->nlmsg_len < NLMSG_LENGTH(sizeof(ifinfomsg))) return false;
  const auto* info = static_cast<const ifinfomsg*>(NLMSG_DATA(header));
  Link link;
  link.up = info->ifi_flags & IFF_UP;
  link.loopback = info->ifi_flags & IFF_LOOPBACK;
  const bool ok = attributes(IFLA_RTA(info), IFLA_PAYLOAD(header), [&](int type, const void* data, size_t size) {
    if (type == IFLA_IFNAME) return stringAttribute(data, size, link.name);
    if (type == IFLA_LINKINFO) return attributes(data, static_cast<int>(size), [&](int nested, const void* value, size_t length) {
      return nested != IFLA_INFO_KIND || stringAttribute(value, length, link.kind);
    });
    return true;
  });
  if (!ok || link.name.empty()) return false;
  links[info->ifi_index] = link;
  return true;
}

inline std::string addressPrefix(int family, const void* address, size_t size, unsigned int length) {
  if (size != (family == AF_INET ? 4u : 16u)) return {};
  char buffer[INET6_ADDRSTRLEN]{};
  if (!inet_ntop(family, address, buffer, sizeof(buffer))) return {};
  return std::string(buffer) + "/" + std::to_string(length);
}

inline bool decodeAddress(const nlmsghdr* header, std::vector<PrefixSource>& sources) {
  if (header->nlmsg_len < NLMSG_LENGTH(sizeof(ifaddrmsg))) return false;
  const auto* info = static_cast<const ifaddrmsg*>(NLMSG_DATA(header));
  if (info->ifa_family != AF_INET && info->ifa_family != AF_INET6) return true;
  unsigned int flags = info->ifa_flags;
  std::string local, peer;
  const bool ok = attributes(IFA_RTA(info), IFA_PAYLOAD(header), [&](int type, const void* data, size_t size) {
    if (type == IFA_FLAGS) return uintAttribute(data, size, flags);
    if (type == IFA_LOCAL || type == IFA_ADDRESS) {
      auto& value = type == IFA_LOCAL ? local : peer;
      value = addressPrefix(info->ifa_family, data, size, info->ifa_prefixlen);
      return !value.empty();
    }
    return true;
  });
  if (!ok) return false;
  if (flags & (IFA_F_TENTATIVE | IFA_F_DADFAILED)) return true;
  if (local.empty()) local = peer;
  if (!local.empty()) sources.push_back({local, static_cast<int>(info->ifa_index), 0, true});
  // Point-to-point peers may differ from the local address.
  if (!peer.empty() && peer != local) sources.push_back({peer, static_cast<int>(info->ifa_index), 0, true});
  return true;
}

inline bool decodeRoute(const nlmsghdr* header, std::vector<PrefixSource>& sources) {
  if (header->nlmsg_len < NLMSG_LENGTH(sizeof(rtmsg))) return false;
  const auto* info = static_cast<const rtmsg*>(NLMSG_DATA(header));
  if ((info->rtm_family != AF_INET && info->rtm_family != AF_INET6) ||
      info->rtm_type != RTN_UNICAST || info->rtm_dst_len <= 1 || info->rtm_src_len != 0 ||
      info->rtm_scope == RT_SCOPE_HOST || (info->rtm_flags & RTM_F_CLONED)) return true;
  unsigned int table = info->rtm_table, interfaceIndex = 0;
  bool direct = true;
  std::string cidr;
  const bool ok = attributes(RTM_RTA(info), RTM_PAYLOAD(header), [&](int type, const void* data, size_t size) {
    if (type == RTA_TABLE) return uintAttribute(data, size, table);
    if (type == RTA_OIF) return uintAttribute(data, size, interfaceIndex);
    if (type == RTA_GATEWAY || type == RTA_VIA || type == RTA_MULTIPATH || type == RTA_NH_ID) direct = false;
    if (type == RTA_DST) {
      cidr = addressPrefix(info->rtm_family, data, size, info->rtm_dst_len);
      return !cidr.empty();
    }
    return true;
  });
  if (!ok) return false;
  if (!cidr.empty() && interfaceIndex != 0)
    sources.push_back({cidr, static_cast<int>(interfaceIndex), table, direct});
  return true;
}

// A subscribed dump socket detects interleaved changes during the three dumps.
// Never publish partial/truncated/interrupted snapshots as an empty LAN set.
inline bool queryPrefixes(const std::string& overlay, unsigned int overlayTable,
                          Prefixes& output, std::string& error) {
  const int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
  if (fd < 0) { error = "underlay netlink socket failed"; return false; }
  struct Close { int fd; ~Close() { close(fd); } } closer{fd};
  sockaddr_nl local{};
  local.nl_family = AF_NETLINK;
  local.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR |
                    RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_ROUTE;
  if (bind(fd, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
    error = "underlay netlink bind failed"; return false;
  }
  std::map<int, Link> links;
  std::vector<PrefixSource> sources;
  unsigned int sequence = 0;
  alignas(nlmsghdr) char buffer[65536];
  for (const auto requestType : {RTM_GETLINK, RTM_GETADDR, RTM_GETROUTE}) {
    struct { nlmsghdr header; union { ifinfomsg link; ifaddrmsg address; rtmsg route; } body; } request{};
    request.header.nlmsg_len = NLMSG_LENGTH(requestType == RTM_GETLINK ? sizeof(ifinfomsg) :
      requestType == RTM_GETADDR ? sizeof(ifaddrmsg) : sizeof(rtmsg));
    request.header.nlmsg_type = requestType;
    request.header.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    request.header.nlmsg_seq = ++sequence;
    request.body.link.ifi_family = AF_UNSPEC;
    sockaddr_nl kernel{};
    kernel.nl_family = AF_NETLINK;
    if (sendto(fd, &request, request.header.nlmsg_len, 0, reinterpret_cast<sockaddr*>(&kernel), sizeof(kernel)) < 0) {
      error = "underlay dump request failed"; return false;
    }
    bool done = false;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!done) {
      if (std::chrono::steady_clock::now() >= deadline) { error = "underlay dump timed out"; return false; }
      pollfd descriptor{fd, POLLIN, 0};
      const int ready = poll(&descriptor, 1, 100);
      if (ready < 0 && errno == EINTR) continue;
      if (ready < 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) {
        error = "underlay dump poll failed"; return false;
      }
      if (ready == 0) continue;
      sockaddr_nl sender{};
      iovec iov{buffer, sizeof(buffer)};
      msghdr message{};
      message.msg_name = &sender; message.msg_namelen = sizeof(sender);
      message.msg_iov = &iov; message.msg_iovlen = 1;
      const auto count = recvmsg(fd, &message, 0);
      if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
      if (count <= 0 || (message.msg_flags & MSG_TRUNC) || sender.nl_pid != 0) {
        error = "underlay dump lost or truncated"; return false;
      }
      int remaining = static_cast<int>(count);
      for (auto* header = reinterpret_cast<nlmsghdr*>(buffer);
           remaining >= static_cast<int>(sizeof(nlmsghdr)) && header->nlmsg_len >= sizeof(nlmsghdr) &&
             header->nlmsg_len <= static_cast<unsigned int>(remaining);
           header = NLMSG_NEXT(header, remaining)) {
        if (header->nlmsg_seq != sequence || (header->nlmsg_flags & NLM_F_DUMP_INTR)) {
          error = "underlay changed during dump; retrying"; return false;
        }
        if (header->nlmsg_type == NLMSG_DONE) {
          int status = 0;
          if (NLMSG_PAYLOAD(header, 0) >= sizeof(status)) std::memcpy(&status, NLMSG_DATA(header), sizeof(status));
          if (status != 0) { error = "underlay dump incomplete"; return false; }
          done = true;
        } else {
          const bool ok = requestType == RTM_GETLINK && header->nlmsg_type == RTM_NEWLINK ? decodeLink(header, links) :
            requestType == RTM_GETADDR && header->nlmsg_type == RTM_NEWADDR ? decodeAddress(header, sources) :
            requestType == RTM_GETROUTE && header->nlmsg_type == RTM_NEWROUTE ? decodeRoute(header, sources) : false;
          if (!ok) { error = "underlay dump malformed or rejected"; return false; }
        }
      }
      if (remaining != 0) { error = "underlay dump malformed tail"; return false; }
    }
  }
  // Any notification queued after the last DONE invalidates this snapshot.
  const auto pending = recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
  if (pending >= 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
    error = "underlay changed after dump; retrying"; return false;
  }
  output = selectPrefixes(links, sources, overlay, overlayTable);
  return true;
}

} // namespace zt_underlay
