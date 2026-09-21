// Link with --gc-sections: unused libzt lifecycle code is discarded, while
// tests exercise the real daemon's manifest/install/verify/cleanup functions.
#define main unused_daemon_main
#include "../src/zt-globald.cpp"
#undef main
#include <cassert>
#include <sys/resource.h>

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string mode = argv[1];
  Config config;
  RoutingSnapshot snapshot;
  snapshot.addresses = {"172.30.0.2/24", "fd88::2/64"};
  snapshot.routes = {{"192.168.50.0/24", "", false, 0}, {"192.168.60.0/24", "", false, 0},
                     {"2001:db8:50::/64", "", true, 0}};
  std::string error;
  assert(zt_underlay::queryPrefixes("zt0", 51820, snapshot.underlay_cidrs, error));
  const auto contains = [&](const std::string& prefix) {
    return std::find(snapshot.underlay_cidrs.begin(), snapshot.underlay_cidrs.end(),
      std::make_pair(prefix.find(':') != std::string::npos, prefix)) != snapshot.underlay_cidrs.end();
  };
  assert(contains("10.77.3.1/32")); // route without a slash in ip text output
  assert(contains("10.77.3.2/32")); // point-to-point style local address
  assert(!contains("10.99.0.0/16")); // renamed virtual interface
  assert(!contains("192.168.60.0/24")); // overlay is never an underlay
  assert(!contains("0.0.0.0/0"));
  if (mode == "down") {
    assert(!contains("192.168.50.0/24"));
    assert(!contains("2001:db8:50::/64"));
    return 0;
  }
  assert(contains("192.168.50.0/24"));
  assert(contains("2001:db8:50::/64"));
  assert(outputHasRule("79: from all to 10.77.3.1 goto 81\n", 79, "10.77.3.1/32", "goto", "81"));
  assert(!outputHasRule("179: from all to 10.77.3.1 goto 810\n", 79, "10.77.3.1/32", "goto", "81"));
  const auto entries = desiredOwnedEntries(config, 80, snapshot);
  if (mode == "install") {
    assert(choosePriority(config) == 80);
    assert(writeOwnedRouteState(entries));
    const auto restored = loadOwnedRouteState();
    assert(restored.size() == entries.size());
    for (size_t i = 0; i < entries.size(); ++i) assert(serializeOwnedEntry(restored[i]) == serializeOwnedEntry(entries[i]));
    assert(installManagedRoutes(config, entries));
    assert(verifyRoutingState(config, 80, snapshot));
  } else if (mode == "cleanup") {
    cleanupOwnedRoutingState(true);
    assert(loadOwnedRouteState().empty());
  } else if (mode == "missing-anchor") {
    assert(!verifyRoutingState(config, 80, snapshot));
    cleanupOwnedRoutingState(true);
    assert(installManagedRoutes(config, entries));
    assert(writeOwnedRouteState(entries));
    assert(verifyRoutingState(config, 80, snapshot));
  } else if (mode == "parser") {
    // Truncated messages/attributes must not produce a valid empty snapshot.
    alignas(nlmsghdr) char data[128]{};
    auto* header = reinterpret_cast<nlmsghdr*>(data);
    std::map<int, zt_underlay::Link> links;
    std::vector<zt_underlay::PrefixSource> sources;
    assert(!zt_underlay::decodeLink(header, links));
    assert(!zt_underlay::decodeAddress(header, sources));
    assert(!zt_underlay::decodeRoute(header, sources));
    assert(!zt_underlay::attributes(data, 1, [](int, const void*, size_t) { return true; }));
    // Failed probe retains its caller's last complete result, not an empty LAN.
    struct rlimit saved{};
    assert(getrlimit(RLIMIT_NOFILE, &saved) == 0);
    auto limited = saved;
    limited.rlim_cur = 0;
    assert(setrlimit(RLIMIT_NOFILE, &limited) == 0);
    const auto before = snapshot.underlay_cidrs;
    assert(!zt_underlay::queryPrefixes("zt0", 51820, snapshot.underlay_cidrs, error));
    assert(snapshot.underlay_cidrs == before);
    assert(setrlimit(RLIMIT_NOFILE, &saved) == 0);
  } else if (mode == "fail-install") {
    auto broken = entries;
    auto bypass = std::find_if(broken.begin(), broken.end(), [](const OwnedRouteEntry& entry) {
      return entry.kind == OwnedKind::Bypass;
    });
    assert(bypass != broken.end());
    bypass->table = 1; // kernel rejects backward goto
    assert(!installManagedRoutes(config, broken));
    const auto rules = commandOutput("ip rule show");
    assert(rules.find("lookup 51820") == std::string::npos);
    assert(zt_underlay::changeBypassRule(false, 81, "", 0, false));
    assert(zt_underlay::changeBypassRule(true, 81, "", 0, false));
  }
  std::cout << "underlay integration: " << mode << " passed\n";
}
