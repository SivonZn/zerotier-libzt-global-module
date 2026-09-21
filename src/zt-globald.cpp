#include <ZeroTierSockets.h>
#include "zt_frame_adapter.h"
#include "managed_route_policy.h"
#include "underlay_netlink.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <linux/if_tun.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <net/if.h>
#include <signal.h>
#include <poll.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

struct Config {
  std::string interface_name = "zt0";
  std::string route_mode = "managed";
  std::string rule_priority = "auto";
  std::string network_id;
  std::string planet_path = "/data/adb/zt-global/planet";
  std::string roots_path = "/data/adb/zt-global/roots";
  std::string storage_path = "/data/adb/zt-global/state";
  int table = 51820;
  int mtu = 1400;
  int port = 0;
  bool enabled = true;
  bool auto_reconnect = true;
};

struct RouteEntry {
  std::string target;
  std::string via;
  bool ipv6 = false;
  uint16_t metric = 0;
};

struct RoutingSnapshot {
  bool policy_rejected = false;
  bool underlay_failed = false;
  std::vector<std::string> addresses;
  std::vector<RouteEntry> routes;
  std::vector<std::pair<bool, std::string>> underlay_cidrs;
};

enum class OwnedKind { Rule, Route, Address, Bypass, Anchor };

struct OwnedRouteEntry {
  OwnedKind kind;
  bool ipv6 = false;
  int priority = 0;
  int table = 0;
  std::string target;
  std::string via;
  std::string device;
};

std::atomic<bool> g_running{true};
int g_tap_fd = -1;
uint64_t g_network_id = 0;
std::mutex g_io_mutex;
std::string g_control_path = "/data/adb/zt-global/runtime/control.sock";
std::atomic<int> g_multicast_member_count{0};
std::atomic<bool> g_multicast_sync_ok{false};
std::atomic<bool> g_node_started{false};
std::atomic<bool> g_planet_loaded{false};
std::string g_activation_id;
std::atomic<bool> g_tap_ready{false};
std::atomic<bool> g_network_ready{false};
std::atomic<bool> g_route_sync_ok{false};
std::atomic<bool> g_frame_bridge_ready{false};
std::atomic<bool> g_route_sync_requested{false};
std::mutex g_status_mutex;
std::string g_last_error;
int g_instance_lock_fd = -1;
const char* kPidPath = "/data/adb/zt-global/runtime/zt-globald.pid";
const char* kLockPath = "/data/adb/zt-global/runtime/zt-globald.lock";
const char* kRouteStatePath = "/data/adb/zt-global/runtime/routes.state";
constexpr std::chrono::seconds kLibztSnapshotInterval(30);
constexpr std::chrono::seconds kFallbackVerifyInterval(120);
constexpr size_t kMaxPlanetSize = 8480;

std::string trim(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return {};
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

bool parseBool(const std::string& value, bool fallback) {
  if (value == "1" || value == "true" || value == "yes" || value == "on") return true;
  if (value == "0" || value == "false" || value == "no" || value == "off") return false;
  return fallback;
}

bool loadConfig(const std::string& path, Config* config) {
  std::ifstream input(path);
  if (!input) return false;
  std::string line;
  while (std::getline(input, line)) {
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    const auto equal = line.find('=');
    if (equal == std::string::npos) continue;
    const auto key = trim(line.substr(0, equal));
    const auto value = trim(line.substr(equal + 1));
    if (key == "enabled") config->enabled = parseBool(value, config->enabled);
    else if (key == "interface") config->interface_name = value;
    else if (key == "route_mode") config->route_mode = value;
    else if (key == "rule_priority") config->rule_priority = value;
    else if (key == "network_id") config->network_id = value;
    else if (key == "planet_path") config->planet_path = value;
    else if (key == "roots_path") config->roots_path = value;
    else if (key == "storage_path") config->storage_path = value;
    else if (key == "routing_table") config->table = std::atoi(value.c_str());
    else if (key == "mtu") config->mtu = std::atoi(value.c_str());
    else if (key == "port") config->port = std::atoi(value.c_str());
    else if (key == "auto_reconnect") config->auto_reconnect = parseBool(value, config->auto_reconnect);
  }
  return true;
}

void logLine(const std::string& line) {
  std::lock_guard<std::mutex> lock(g_io_mutex);
  std::cerr << line << std::endl;
}

void setLastError(const std::string& error) {
  std::lock_guard<std::mutex> lock(g_status_mutex);
  g_last_error = error;
}

void clearLastError() {
  std::lock_guard<std::mutex> lock(g_status_mutex);
  g_last_error.clear();
}

std::string lastError() {
  std::lock_guard<std::mutex> lock(g_status_mutex);
  return g_last_error;
}

std::string jsonEscape(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (const char ch : value) {
    if (ch == '\\' || ch == '"') escaped.push_back('\\');
    if (ch == '\n') escaped += "\\n";
    else if (ch != '\r') escaped.push_back(ch);
  }
  return escaped;
}

bool runCommand(const std::string& command) {
  logLine("exec: " + command);
  const int status = std::system(command.c_str());
  if (status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
  const std::string error = "command failed: " + command;
  logLine(error);
  setLastError(error);
  return false;
}

std::string commandOutput(const std::string& command, bool* ok = nullptr) {
  FILE* pipe = popen(command.c_str(), "r");
  std::string output;
  char line[1024];
  while (pipe && fgets(line, sizeof(line), pipe)) output += line;
  const int status = pipe ? pclose(pipe) : -1;
  const bool success = status != -1 && WIFEXITED(status) && WEXITSTATUS(status) == 0;
  if (ok) *ok = success;
  return output;
}

bool runCleanupCommand(const std::string& command) {
  logLine("cleanup: " + command);
  const int status = std::system((command + " >/dev/null 2>&1").c_str());
  return status != -1;
}

bool writeAll(int fd, const void* data, size_t size) {
  const auto* cursor = static_cast<const uint8_t*>(data);
  while (size > 0) {
    const ssize_t written = write(fd, cursor, size);
    if (written < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    cursor += written;
    size -= static_cast<size_t>(written);
  }
  return true;
}

bool copyFileAtomic(const std::string& source, const std::string& destination, mode_t mode) {
  const int input = open(source.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
  if (input < 0) return false;
  const std::string temporary = destination + ".tmp." + std::to_string(getpid());
  const int output = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, mode);
  if (output < 0) {
    close(input);
    return false;
  }
  bool ok = true;
  char buffer[4096];
  for (;;) {
    const ssize_t count = read(input, buffer, sizeof(buffer));
    if (count == 0) break;
    if (count < 0) {
      if (errno == EINTR) continue;
      ok = false;
      break;
    }
    if (!writeAll(output, buffer, static_cast<size_t>(count))) {
      ok = false;
      break;
    }
  }
  if (ok && fsync(output) != 0) ok = false;
  close(input);
  close(output);
  if (ok) {
    chmod(temporary.c_str(), mode);
    ok = rename(temporary.c_str(), destination.c_str()) == 0;
  }
  if (!ok) unlink(temporary.c_str());
  return ok;
}

bool anyNonzero(const std::vector<uint8_t>& bytes, size_t offset, size_t length) {
  if (offset + length > bytes.size()) return false;
  for (size_t index = offset; index < offset + length; ++index)
    if (bytes[index] != 0) return true;
  return false;
}

bool validatePlanetBytes(const std::vector<uint8_t>& bytes, std::string* reason) {
  const auto fail = [reason](const char* message) {
    if (reason) *reason = message;
    return false;
  };
  if (bytes.size() < 178 || bytes.size() > kMaxPlanetSize) return fail("invalid_planet_size");
  size_t offset = 0;
  if (bytes[offset++] != 1) return fail("planet_type_required");
  if (!anyNonzero(bytes, offset, 8)) return fail("invalid_planet_id");
  offset += 8;
  if (!anyNonzero(bytes, offset, 8)) return fail("invalid_planet_timestamp");
  offset += 8;
  if (!anyNonzero(bytes, offset, 64)) return fail("invalid_planet_update_key");
  offset += 64;
  if (!anyNonzero(bytes, offset, 96)) return fail("invalid_planet_signature");
  offset += 96;
  if (offset >= bytes.size()) return fail("truncated_planet");
  const unsigned int roots = bytes[offset++];
  if (roots == 0 || roots > 4) return fail("invalid_planet_root_count");
  unsigned int totalEndpoints = 0;
  for (unsigned int root = 0; root < roots; ++root) {
    if (offset + 71 > bytes.size()) return fail("truncated_planet_identity");
    if (!anyNonzero(bytes, offset, 5)) return fail("invalid_planet_root_address");
    offset += 5;
    if (bytes[offset++] != 0) return fail("invalid_planet_identity_type");
    if (!anyNonzero(bytes, offset, 64)) return fail("invalid_planet_root_key");
    offset += 64;
    if (bytes[offset++] != 0) return fail("private_key_not_allowed_in_planet");
    if (offset >= bytes.size()) return fail("truncated_planet_endpoints");
    const unsigned int endpoints = bytes[offset++];
    if (endpoints > 32) return fail("invalid_planet_endpoint_count");
    totalEndpoints += endpoints;
    for (unsigned int endpoint = 0; endpoint < endpoints; ++endpoint) {
      if (offset >= bytes.size()) return fail("truncated_planet_endpoint");
      const uint8_t type = bytes[offset++];
      size_t addressLength = 0;
      if (type == 4) addressLength = 4;
      else if (type == 6) addressLength = 16;
      else return fail("invalid_planet_endpoint_type");
      if (offset + addressLength + 2 > bytes.size()) return fail("truncated_planet_endpoint");
      if (!anyNonzero(bytes, offset, addressLength)) return fail("invalid_planet_endpoint_address");
      offset += addressLength;
      if (bytes[offset] == 0 && bytes[offset + 1] == 0) return fail("invalid_planet_endpoint_port");
      offset += 2;
    }
  }
  if (totalEndpoints == 0) return fail("planet_has_no_endpoints");
  if (offset != bytes.size()) return fail("planet_has_trailing_data");
  return true;
}

bool validatePlanetFile(const std::string& path, std::string* reason) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    if (reason) *reason = "planet_open_failed";
    return false;
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  return validatePlanetBytes(bytes, reason);
}

std::string parentDirectory(const std::string& path) {
  const auto slash = path.find_last_of('/');
  return slash == std::string::npos ? std::string(".") : path.substr(0, slash);
}

bool prepareIdentity(const Config& config) {
  const std::string currentPublic = config.storage_path + "/identity.public";
  const std::string currentSecret = config.storage_path + "/identity.secret";
  const std::string backupDirectory = parentDirectory(config.storage_path) + "/identity-backup";
  const std::string backupPublic = backupDirectory + "/identity.public";
  const std::string backupSecret = backupDirectory + "/identity.secret";
  const bool hasPublic = access(currentPublic.c_str(), F_OK) == 0;
  const bool hasSecret = access(currentSecret.c_str(), F_OK) == 0;
  const bool hasBackupPublic = access(backupPublic.c_str(), F_OK) == 0;
  const bool hasBackupSecret = access(backupSecret.c_str(), F_OK) == 0;
  if (hasPublic != hasSecret) {
    setLastError("identity_incomplete");
    logLine("refusing to start with an incomplete ZeroTier identity");
    return false;
  }
  if (!hasPublic && hasBackupPublic != hasBackupSecret) {
    setLastError("identity_backup_incomplete");
    logLine("refusing to start with an incomplete ZeroTier identity backup");
    return false;
  }
  if (!hasPublic && hasBackupPublic && hasBackupSecret) {
    mkdir(config.storage_path.c_str(), 0700);
    if (!copyFileAtomic(backupPublic, currentPublic, 0600) ||
        !copyFileAtomic(backupSecret, currentSecret, 0600)) {
      setLastError("identity_restore_failed");
      return false;
    }
    logLine("restored ZeroTier identity from protected backup");
  }
  return true;
}

bool backupIdentity(const Config& config) {
  const std::string currentPublic = config.storage_path + "/identity.public";
  const std::string currentSecret = config.storage_path + "/identity.secret";
  if (access(currentPublic.c_str(), R_OK) != 0 || access(currentSecret.c_str(), R_OK) != 0) {
    setLastError("identity_files_missing_after_start");
    return false;
  }
  const std::string backupDirectory = parentDirectory(config.storage_path) + "/identity-backup";
  if (mkdir(backupDirectory.c_str(), 0700) != 0 && errno != EEXIST) {
    setLastError("identity_backup_directory_failed");
    return false;
  }
  chmod(backupDirectory.c_str(), 0700);
  const bool ok = copyFileAtomic(currentPublic, backupDirectory + "/identity.public", 0600) &&
                  copyFileAtomic(currentSecret, backupDirectory + "/identity.secret", 0600);
  if (!ok) setLastError("identity_backup_failed");
  return ok;
}

uint64_t parseNetworkId(const std::string& value) {
  char* end = nullptr;
  errno = 0;
  const auto parsed = std::strtoull(value.c_str(), &end, 16);
  if (errno != 0 || end == value.c_str() || *end != '\0') return 0;
  return static_cast<uint64_t>(parsed);
}

int openTap(const Config& config) {
  const int fd = open("/dev/net/tun", O_RDWR | O_CLOEXEC);
  if (fd < 0) {
    logLine("open /dev/net/tun failed: " + std::string(std::strerror(errno)));
    return -1;
  }
  struct ifreq request{};
  std::snprintf(request.ifr_name, IFNAMSIZ, "%s", config.interface_name.c_str());
  request.ifr_flags = IFF_TAP | IFF_NO_PI;
  if (ioctl(fd, TUNSETIFF, &request) < 0) {
    logLine("TUNSETIFF failed: " + std::string(std::strerror(errno)));
    close(fd);
    return -1;
  }
  const int flags = fcntl(fd, F_GETFL, 0);
  if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  char networkMac[ZTS_MAC_ADDRSTRLEN] = {};
  if (zts_net_get_mac_str(g_network_id, networkMac, sizeof(networkMac)) == ZTS_ERR_OK &&
      networkMac[0] != '\0') {
    if (!runCommand("ip link set dev " + config.interface_name + " address " + networkMac)) {
      close(fd);
      return -1;
    }
    logLine("configured ZeroTier MAC " + std::string(networkMac));
  } else {
    logLine("unable to read ZeroTier MAC");
    setLastError("unable to read ZeroTier MAC");
    close(fd);
    return -1;
  }
  if (!runCommand("ip link set dev " + config.interface_name + " mtu " + std::to_string(config.mtu) + " up")) {
    close(fd);
    return -1;
  }
  return fd;
}

int choosePriority(const Config& config) {
  int lowestVpn = 32767;
  int used[32768] = {};
  for (const auto* command : {"ip rule show 2>/dev/null", "ip -6 rule show 2>/dev/null"}) {
    bool ok = false;
    const auto output = commandOutput(command, &ok);
    if (!ok) return 0;
    std::istringstream lines(output);
    std::string line;
    while (std::getline(lines, line)) {
      int priority = -1;
      if (std::sscanf(line.c_str(), "%d:", &priority) == 1 && priority > 0 && priority < 32768) {
        used[priority] = 1;
        if (line.find("tun") != std::string::npos || line.find("vpn") != std::string::npos ||
            line.find("fwmark") != std::string::npos) lowestVpn = std::min(lowestVpn, priority);
      }
    }
  }
  const auto available = [&](int p) { return p >= 2 && p <= 32764 &&
    p + 1 < lowestVpn && !used[p - 1] && !used[p] && !used[p + 1]; };
  if (config.rule_priority != "auto") {
    const int p = std::atoi(config.rule_priority.c_str());
    return available(p) ? p : 0;
  }
  // Reserve the bypass/overlay/continuation band in BOTH address families.
  int candidate = std::min(80, lowestVpn - 2);
  while (candidate >= 2 && !available(candidate)) --candidate;
  return candidate >= 2 ? candidate : 0;
}

bool safeManifestField(const std::string& value) {
  if (value.empty() || value.size() > 160) return false;
  for (const char ch : value) {
    const bool allowed = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                         (ch >= '0' && ch <= '9') || ch == '.' || ch == ':' ||
                         ch == '/' || ch == '_' || ch == '-';
    if (!allowed) return false;
  }
  return true;
}

std::string serializeOwnedEntry(const OwnedRouteEntry& entry) {
  std::ostringstream line;
  if (entry.kind == OwnedKind::Rule) {
    line << "rule|" << (entry.ipv6 ? 6 : 4) << '|' << entry.priority << '|'
         << entry.target << '|' << entry.table;
  } else if (entry.kind == OwnedKind::Bypass || entry.kind == OwnedKind::Anchor) {
    line << (entry.kind == OwnedKind::Bypass ? "bypass|" : "anchor|")
         << (entry.ipv6 ? 6 : 4) << '|' << entry.priority << '|'
         << (entry.target.empty() ? "-" : entry.target) << '|' << entry.table;
  } else if (entry.kind == OwnedKind::Route) {
    line << "route|" << (entry.ipv6 ? 6 : 4) << '|' << entry.table << '|'
         << entry.target << '|' << (entry.via.empty() ? "-" : entry.via) << '|' << entry.device;
  } else {
    line << "addr|" << (entry.ipv6 ? 6 : 4) << '|' << entry.target << '|' << entry.device;
  }
  return line.str();
}

std::vector<std::string> splitFields(const std::string& value) {
  std::vector<std::string> fields;
  std::istringstream input(value);
  std::string field;
  while (std::getline(input, field, '|')) fields.push_back(field);
  return fields;
}

bool parsePositiveInt(const std::string& value, int* result) {
  if (value.empty()) return false;
  char* end = nullptr;
  errno = 0;
  const long parsed = std::strtol(value.c_str(), &end, 10);
  if (errno != 0 || end == value.c_str() || *end != '\0' || parsed <= 0 || parsed > 2147483647L) return false;
  *result = static_cast<int>(parsed);
  return true;
}

std::vector<OwnedRouteEntry> loadOwnedRouteState() {
  std::vector<OwnedRouteEntry> entries;
  std::ifstream input(kRouteStatePath);
  std::string line;
  while (std::getline(input, line)) {
    const auto fields = splitFields(trim(line));
    if (fields.empty()) continue;
    OwnedRouteEntry entry{};
    if ((fields[0] == "bypass" || fields[0] == "anchor") && fields.size() == 5) {
      entry.kind = fields[0] == "bypass" ? OwnedKind::Bypass : OwnedKind::Anchor;
      entry.ipv6 = fields[1] == "6";
      if ((fields[1] != "4" && fields[1] != "6") || !parsePositiveInt(fields[2], &entry.priority)) continue;
      if (entry.kind == OwnedKind::Bypass) {
        zt_policy::Prefix prefix;
        if (!zt_policy::parsePrefix(fields[3], prefix) || prefix.ipv6 != entry.ipv6 ||
            !parsePositiveInt(fields[4], &entry.table) || entry.table <= entry.priority) continue;
        entry.target = fields[3];
      } else if (fields[3] != "-" || fields[4] != "0") continue;
    } else if (fields[0] == "rule" && fields.size() == 5) {
      entry.kind = OwnedKind::Rule;
      entry.ipv6 = fields[1] == "6";
      if ((fields[1] != "4" && fields[1] != "6") ||
          !parsePositiveInt(fields[2], &entry.priority) ||
          !safeManifestField(fields[3]) || !parsePositiveInt(fields[4], &entry.table)) continue;
      entry.target = fields[3];
    } else if (fields[0] == "route" && fields.size() == 6) {
      entry.kind = OwnedKind::Route;
      entry.ipv6 = fields[1] == "6";
      if ((fields[1] != "4" && fields[1] != "6") || !parsePositiveInt(fields[2], &entry.table) ||
          !safeManifestField(fields[3]) || !safeManifestField(fields[4]) || !safeManifestField(fields[5])) continue;
      entry.target = fields[3];
      entry.via = fields[4] == "-" ? "" : fields[4];
      entry.device = fields[5];
    } else if (fields[0] == "addr" && fields.size() == 4) {
      entry.kind = OwnedKind::Address;
      entry.ipv6 = fields[1] == "6";
      if ((fields[1] != "4" && fields[1] != "6") ||
          !safeManifestField(fields[2]) || !safeManifestField(fields[3])) continue;
      entry.target = fields[2];
      entry.device = fields[3];
    } else {
      continue;
    }
    entries.push_back(entry);
  }
  return entries;
}

bool writeOwnedRouteState(const std::vector<OwnedRouteEntry>& entries) {
  const std::string temporary = std::string(kRouteStatePath) + ".tmp." + std::to_string(getpid());
  const int fd = open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
  if (fd < 0) return false;
  std::string contents;
  std::set<std::string> unique;
  for (const auto& entry : entries) {
    const std::string line = serializeOwnedEntry(entry);
    if (unique.insert(line).second) contents += line + "\n";
  }
  bool ok = writeAll(fd, contents.data(), contents.size()) && fsync(fd) == 0;
  close(fd);
  if (ok) ok = rename(temporary.c_str(), kRouteStatePath) == 0;
  if (!ok) unlink(temporary.c_str());
  return ok;
}

std::string ownedEntryCommand(const OwnedRouteEntry& entry, bool install) {
  const std::string tool = entry.ipv6 ? "ip -6" : "ip";
  if (entry.kind == OwnedKind::Rule) {
    // Generated command is intentionally the same `rule add pref` form used
    // by Android's iproute2 when install=true.
    return tool + " rule " + (install ? "add" : "del") + " pref " +
        std::to_string(entry.priority) + " to " + entry.target + " lookup " +
        (entry.table == 254 ? std::string("main") : std::to_string(entry.table));
  }
  if (entry.kind == OwnedKind::Route) {
    std::string command = tool + " route " + (install ? "replace" : "del") + " " + entry.target;
    if (!entry.via.empty()) command += " via " + entry.via;
    return command + " dev " + entry.device + " table " + std::to_string(entry.table);
  }
  return tool + " addr " + (install ? "replace" : "del") + " " + entry.target + " dev " + entry.device +
    (install ? " noprefixroute" : "");
}

bool outputHasRule(const std::string& output, int priority, const std::string& target,
                   const std::string& action, const std::string& argument = "") {
  std::istringstream lines(output);
  std::string line;
  while (std::getline(lines, line)) {
    std::istringstream tokens(line);
    std::string token, destination, actualArgument;
    if (!(tokens >> token) || token != std::to_string(priority) + ":") continue;
    bool foundAction = false;
    while (tokens >> token) {
      if (token == "to") tokens >> destination;
      if (token == action) {
        foundAction = true;
        if (!argument.empty()) tokens >> actualArgument;
      }
    }
    if (!target.empty()) {
      if (destination.find('/') == std::string::npos)
        destination += destination.find(':') == std::string::npos ? "/32" : "/128";
      if (zt_underlay::canonicalPrefix(destination) != zt_underlay::canonicalPrefix(target)) continue;
    }
    if (foundAction && actualArgument == argument) return true;
  }
  return false;
}

bool applyOwnedEntry(const OwnedRouteEntry& entry, bool install) {
  if (entry.kind == OwnedKind::Bypass || entry.kind == OwnedKind::Anchor) {
    const bool ok = zt_underlay::changeBypassRule(entry.ipv6, entry.priority, entry.target,
      entry.kind == OwnedKind::Bypass ? entry.table : 0, install);
    logLine(std::string(install ? "install: " : "cleanup: ") + serializeOwnedEntry(entry) + (ok ? "" : " FAILED"));
    if (!ok && install) setLastError("underlay bypass rule installation failed");
    return ok;
  }
  if (install) return runCommand(ownedEntryCommand(entry, true));
  return runCleanupCommand(ownedEntryCommand(entry, false));
}

bool cleanupOwnedEntries(const std::vector<OwnedRouteEntry>& entries) {
  // Withdraw overlay selectors before removing bypasses/anchors, regardless
  // of recovery-manifest order after an interrupted synchronization.
  for (const auto& entry : entries) {
    if (entry.kind != OwnedKind::Rule) continue;
    const auto command = std::string(entry.ipv6 ? "ip -6" : "ip") + " rule show 2>/dev/null";
    bool ok = false;
    auto rules = commandOutput(command, &ok);
    if (!ok) { setLastError("unable to verify overlay rule withdrawal"); return false; }
    const auto table = entry.table == 254 ? std::string("main") : std::to_string(entry.table);
    if (!outputHasRule(rules, entry.priority, entry.target, "lookup", table)) continue;
    if (!runCommand(ownedEntryCommand(entry, false))) return false;
    rules = commandOutput(command, &ok);
    if (!ok || outputHasRule(rules, entry.priority, entry.target, "lookup", table)) {
      setLastError("overlay rule withdrawal failed; retaining underlay protection"); return false;
    }
  }
  for (auto iterator = entries.rbegin(); iterator != entries.rend(); ++iterator)
    if (iterator->kind != OwnedKind::Rule && !applyOwnedEntry(*iterator, false)) return false;
  return true;
}

void cleanupOwnedRoutingState(bool removeManifest) {
  const bool ok = cleanupOwnedEntries(loadOwnedRouteState());
  if (removeManifest && ok) unlink(kRouteStatePath);
}

void clearRoutes(const Config& config) {
  cleanupOwnedRoutingState(true);
  runCleanupCommand("ip link set dev " + config.interface_name + " down");
}

void cleanupLegacyModuleRoutes(const Config& config) {
  // 0.3.1 and earlier did not have an ownership manifest. Remove only
  // entries that explicitly point at this module's table and interface.
  for (const bool ipv6 : {false, true}) {
    const std::string tool = ipv6 ? "ip -6" : "ip";
    const std::string rules = commandOutput(tool + " rule show 2>/dev/null");
    std::istringstream ruleLines(rules);
    std::string line;
    while (std::getline(ruleLines, line)) {
      if (line.find("lookup " + std::to_string(config.table)) == std::string::npos) continue;
      std::istringstream tokens(line);
      std::string priorityToken;
      std::string token;
      std::string target;
      tokens >> priorityToken;
      if (priorityToken.empty() || priorityToken.back() != ':') continue;
      priorityToken.pop_back();
      while (tokens >> token) {
        if (token == "to" && tokens >> target) break;
      }
      int priority = 0;
      if (!parsePositiveInt(priorityToken, &priority) || target.empty() || target == "all") continue;
      runCleanupCommand(tool + " rule del pref " + priorityToken + " to " + target +
                        " lookup " + std::to_string(config.table));
    }
    const std::string routes = commandOutput(tool + " -o route show table " + std::to_string(config.table) +
                                             " dev " + config.interface_name + " 2>/dev/null");
    std::istringstream routeLines(routes);
    while (std::getline(routeLines, line)) {
      std::istringstream tokens(line);
      std::string target;
      tokens >> target;
      if (target.empty() || target == "default") continue;
      runCleanupCommand(tool + " route del " + target + " dev " + config.interface_name +
                        " table " + std::to_string(config.table));
    }
  }
  runCleanupCommand("ip addr flush dev " + config.interface_name + " scope global");
}

bool queryRoutingSnapshot(const Config& config, RoutingSnapshot* snapshot,
                          bool refreshUnderlay,
                          std::vector<std::pair<bool, std::string>>* cachedUnderlay) {
  snapshot->addresses.clear();
  snapshot->routes.clear();
  snapshot->policy_rejected = false;
  if (refreshUnderlay) {
    std::string error;
    if (!zt_underlay::queryPrefixes(config.interface_name, config.table, *cachedUnderlay, error)) {
      snapshot->underlay_failed = true;
      setLastError(error);
      return false;
    }
  }
  snapshot->underlay_cidrs = *cachedUnderlay;
  if (zts_core_lock_obtain() < 0) {
    setLastError("unable to lock libzt core for route query");
    return false;
  }
  bool ok = true;
  const int addressCount = zts_core_query_addr_count(g_network_id);
  if (addressCount < 0) ok = false;
  for (int index = 0; ok && index < addressCount; ++index) {
    char address[128] = {};
    if (zts_core_query_addr(g_network_id, static_cast<unsigned int>(index), address,
                            sizeof(address)) < 0 || address[0] == '\0') {
      ok = false;
      break;
    }
    snapshot->addresses.emplace_back(address);
  }
  const int count = zts_core_query_route_count(g_network_id);
  if (count < 0) ok = false;
  for (int index = 0; ok && index < count; ++index) {
    char target[128] = {};
    char via[128] = {};
    uint16_t flags = 0;
    uint16_t metric = 0;
    if (zts_core_query_route(g_network_id, static_cast<unsigned int>(index), target, via,
                             sizeof(target), &flags, &metric) < 0 || target[0] == '\0') {
      ok = false;
      break;
    }
    const std::string targetValue(target);
    snapshot->routes.push_back({targetValue, via, std::strchr(target, ':') != nullptr, metric});
  }
  zts_core_lock_release();
  if (!ok) { setLastError("unable to query libzt routes or addresses"); return false; }
  std::vector<std::string> destinations = snapshot->addresses;
  for (const auto& route : snapshot->routes) destinations.push_back(route.target);
  const auto rejection = zt_policy::managedRoutingRejection(destinations);
  if (!rejection.empty()) {
    snapshot->policy_rejected = true;
    const auto error = "managed route policy rejected: " + rejection;
    if (lastError() != error) logLine(error);
    setLastError(error);
    return false;
  }
  std::stable_sort(snapshot->routes.begin(), snapshot->routes.end(), [](const RouteEntry& left, const RouteEntry& right) {
    const bool leftDirect = left.via.empty() || left.via == "0.0.0.0" || left.via == "::";
    const bool rightDirect = right.via.empty() || right.via == "0.0.0.0" || right.via == "::";
    return leftDirect && !rightDirect;
  });
  return ok;
}

std::string snapshotFingerprint(const RoutingSnapshot& snapshot) {
  std::ostringstream value;
  for (const auto& address : snapshot.addresses) value << "a:" << address << ';';
  for (const auto& route : snapshot.routes)
    value << "r:" << route.target << ':' << route.via << ':' << route.metric << ';';
  for (const auto& cidr : snapshot.underlay_cidrs)
    value << "u:" << cidr.first << ':' << cidr.second << ';';
  return value.str();
}

bool outputHasLine(const std::string& output, const std::vector<std::string>& needles) {
  std::istringstream lines(output);
  std::string line;
  while (std::getline(lines, line)) {
    bool matches = true;
    for (const auto& needle : needles) matches = matches && line.find(needle) != std::string::npos;
    if (matches) return true;
  }
  return false;
}

bool verifyRoutingState(const Config& config, int priority, const RoutingSnapshot& snapshot) {
  const std::string addresses = commandOutput("ip -o addr show dev " + config.interface_name + " 2>/dev/null");
  const std::string rules4 = commandOutput("ip rule show 2>/dev/null");
  const std::string rules6 = commandOutput("ip -6 rule show 2>/dev/null");
  const std::string routes4 = commandOutput("ip route show table " + std::to_string(config.table) + " 2>/dev/null");
  const std::string routes6 = commandOutput("ip -6 route show table " + std::to_string(config.table) + " 2>/dev/null");
  for (const auto& address : snapshot.addresses)
    if (addresses.find(" " + address + " ") == std::string::npos) return false;
  for (const auto& route : snapshot.routes) {
    const auto& routeOutput = route.ipv6 ? routes6 : routes4;
    const auto& ruleOutput = route.ipv6 ? rules6 : rules4;
    if (!outputHasLine(routeOutput, {route.target, "dev " + config.interface_name})) return false;
    if (!outputHasLine(ruleOutput, {std::to_string(priority) + ":", "to " + route.target,
                                    "lookup " + std::to_string(config.table)})) return false;
  }
  const int underlayPriority = std::max(1, priority - 1);
  for (const auto* ruleOutput : {&rules4, &rules6})
    if (!outputHasRule(*ruleOutput, priority + 1, "", "nop")) return false;
  for (const auto& item : snapshot.underlay_cidrs) {
    const auto& ruleOutput = item.first ? rules6 : rules4;
    if (!outputHasRule(ruleOutput, underlayPriority, item.second, "goto", std::to_string(priority + 1))) return false;
  }
  return true;
}

std::vector<OwnedRouteEntry> desiredOwnedEntries(const Config& config, int priority,
                                                  const RoutingSnapshot& snapshot) {
  std::vector<OwnedRouteEntry> entries;
  const int underlayPriority = std::max(1, priority - 1);
  for (const bool ipv6 : {false, true})
    entries.push_back({OwnedKind::Anchor, ipv6, priority + 1, 0, "", "", ""});
  for (const auto& item : snapshot.underlay_cidrs)
    entries.push_back({OwnedKind::Bypass, item.first, underlayPriority, priority + 1, item.second, "", ""});
  for (const auto& address : snapshot.addresses)
    entries.push_back({OwnedKind::Address, address.find(':') != std::string::npos, 0, 0,
                       address, "", config.interface_name});
  for (const auto& route : snapshot.routes) {
    const std::string via = (route.via == "0.0.0.0" || route.via == "::") ? "" : route.via;
    entries.push_back({OwnedKind::Route, route.ipv6, 0, config.table,
                       route.target, via, config.interface_name});
    entries.push_back({OwnedKind::Rule, route.ipv6, priority, config.table,
                       route.target, "", ""});
  }
  return entries;
}

bool installManagedRoutes(const Config& config, const std::vector<OwnedRouteEntry>& entries) {
  if (config.route_mode != "managed") {
    logLine("route_mode is not managed; refusing to install a default route");
    setLastError("route_mode is not managed");
    return false;
  }
  bool ok = runCommand("ip link set dev " + config.interface_name + " mtu " +
                       std::to_string(config.mtu) + " up");
  // Fail fast: never enable an overlay selector after bypass installation failed.
  for (const auto& entry : entries) {
    if (!ok) break;
    ok = applyOwnedEntry(entry, true);
  }
  return ok;
}

bool synchronizeRouting(const Config& config, int priority, std::string* appliedFingerprint,
                        std::vector<std::pair<bool, std::string>>* cachedUnderlay,
                        bool refreshUnderlay, bool forceVerify) {
  RoutingSnapshot snapshot;
  if (priority < 2) {
    setLastError("no free underlay/overlay rule priority band; check rule_priority and restart");
    g_route_sync_ok = false;
    return false;
  }
  if (!queryRoutingSnapshot(config, &snapshot, refreshUnderlay || appliedFingerprint->empty(), cachedUnderlay)) {
    if (snapshot.policy_rejected || snapshot.underlay_failed) {
      // Withdraw owned entries from the previous snapshot as well; never
      // leave a previously accepted catch-all active after policy rejection.
      cleanupOwnedRoutingState(true);
      appliedFingerprint->clear();
    }
    g_network_ready = false;
    g_route_sync_ok = false;
    return false;
  }
  g_network_ready = !snapshot.addresses.empty() && zts_net_transport_is_ready(g_network_id) == 1;
  const std::string fingerprint = snapshotFingerprint(snapshot);
  if (fingerprint == *appliedFingerprint) {
    if (!forceVerify || verifyRoutingState(config, priority, snapshot)) {
      g_route_sync_ok = true;
      if (g_network_ready && g_tap_ready) clearLastError();
      return true;
    }
  }
  const auto previous = loadOwnedRouteState();
  const auto desired = desiredOwnedEntries(config, priority, snapshot);
  g_route_sync_ok = false;
  std::vector<OwnedRouteEntry> recovery = previous;
  recovery.insert(recovery.end(), desired.begin(), desired.end());
  if (!writeOwnedRouteState(recovery)) {
    cleanupOwnedEntries(previous);
    appliedFingerprint->clear();
    setLastError("route_state_write_failed");
    g_route_sync_ok = false;
    return false;
  }
  // Selectors first, then protection, then routes. No overlay takeover is
  // active during the transition, so Android remains in control of the LAN.
  bool ok = cleanupOwnedEntries(previous);
  if (ok) ok = installManagedRoutes(config, desired);
  ok = verifyRoutingState(config, priority, snapshot) && ok;
  if (ok) ok = writeOwnedRouteState(desired);
  g_route_sync_ok = ok;
  if (ok) {
    *appliedFingerprint = fingerprint;
    logLine("managed route synchronization complete; priority=" + std::to_string(priority));
    if (g_network_ready && g_tap_ready) clearLastError();
  } else {
    cleanupOwnedEntries(recovery);
    appliedFingerprint->clear();
    if (lastError().empty()) setLastError("route synchronization failed");
  }
  return ok;
}

void frameFromLibzt(uint64_t network_id, const void* frame, unsigned int length) {
  if (network_id != g_network_id || g_tap_fd < 0 || frame == nullptr || length == 0) return;
  const auto* bytes = static_cast<const uint8_t*>(frame);
  const auto written = write(g_tap_fd, bytes, length);
  if (written < 0 && errno != EAGAIN && errno != EWOULDBLOCK) logLine("TAP write failed");
}

void tapLoop() {
  std::vector<uint8_t> frame(4096);
  while (g_running && g_tap_fd >= 0) {
    const auto received = read(g_tap_fd, frame.data(), frame.size());
    if (received <= 0) {
      if (errno == EINTR) continue;
      std::this_thread::sleep_for(std::chrono::milliseconds(50));
      continue;
    }
    if (zts_net_send_frame) {
      if (zts_net_send_frame(g_network_id, frame.data(), static_cast<unsigned int>(received)) < 0) {
        logLine("libzt frame injection failed");
      }
    } else {
      logLine("libzt frame adapter is missing; control-plane only mode");
      std::this_thread::sleep_for(std::chrono::seconds(5));
    }
  }
}

bool readKernelMulticastGroups(const std::string& interfaceName, std::set<uint64_t>* groups) {
  std::ifstream input("/proc/net/dev_mcast");
  if (!input) return false;
  int interfaceIndex = 0;
  int users = 0;
  int globalUsers = 0;
  std::string device;
  std::string address;
  while (input >> interfaceIndex >> device >> users >> globalUsers >> address) {
    if (device != interfaceName || address.size() != 12) continue;
    char* end = nullptr;
    errno = 0;
    const uint64_t mac = std::strtoull(address.c_str(), &end, 16);
    if (errno != 0 || end == address.c_str() || *end != '\0') continue;
    if (((mac >> 40) & 0x01U) == 0) continue;
    groups->insert(mac);
  }
  return true;
}

void multicastLoop(const Config& config) {
  std::set<uint64_t> installed;
  while (g_running) {
    std::set<uint64_t> observed;
    const bool readOk = readKernelMulticastGroups(config.interface_name, &observed);
    if (readOk) {
      bool applyOk = true;
      for (const uint64_t mac : observed) {
        if (installed.count(mac) == 0 && zts_net_multicast_add(g_network_id, mac, 0) < 0) {
          applyOk = false;
          logLine("unable to add external multicast group");
        }
      }
      for (const uint64_t mac : installed) {
        if (observed.count(mac) == 0 && zts_net_multicast_remove(g_network_id, mac, 0) < 0) {
          applyOk = false;
          logLine("unable to remove external multicast group");
        }
      }
      if (applyOk) installed.swap(observed);
      g_multicast_member_count = static_cast<int>(installed.size());
      g_multicast_sync_ok = applyOk;
    } else {
      g_multicast_sync_ok = false;
    }
    for (int tick = 0; tick < 50 && g_running; ++tick)
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  zts_net_multicast_clear(g_network_id);
  g_multicast_member_count = 0;
}

void netlinkRouteLoop() {
  const int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_ROUTE);
  if (fd < 0) {
    logLine("unable to open rtnetlink listener: " + std::string(std::strerror(errno)));
    return;
  }
  struct sockaddr_nl address{};
  address.nl_family = AF_NETLINK;
  address.nl_pid = 0;
  address.nl_groups = RTMGRP_LINK | RTMGRP_IPV4_IFADDR | RTMGRP_IPV6_IFADDR |
                      RTMGRP_IPV4_ROUTE | RTMGRP_IPV6_ROUTE | RTMGRP_IPV4_RULE |
                      (1u << (RTNLGRP_IPV6_RULE - 1));
  if (bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    logLine("unable to bind rtnetlink listener: " + std::string(std::strerror(errno)));
    close(fd);
    return;
  }
  std::vector<char> buffer(32768);
  while (g_running) {
    struct pollfd descriptor{fd, POLLIN, 0};
    const int ready = poll(&descriptor, 1, 1000);
    if (ready < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (ready == 0 || !(descriptor.revents & POLLIN)) continue;
    for (;;) {
      const ssize_t count = recv(fd, buffer.data(), buffer.size(), MSG_TRUNC);
      if (count < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
        if (errno == EINTR) continue;
        if (errno == ENOBUFS) { g_route_sync_requested = true; continue; }
        close(fd);
        return;
      }
      if (count == 0) break;
      if (static_cast<size_t>(count) > buffer.size()) { g_route_sync_requested = true; continue; }
      int remaining = static_cast<int>(count);
      for (auto* header = reinterpret_cast<struct nlmsghdr*>(buffer.data());
           remaining >= static_cast<int>(sizeof(struct nlmsghdr)) &&
               header->nlmsg_len >= sizeof(struct nlmsghdr) &&
               header->nlmsg_len <= static_cast<unsigned int>(remaining);
           header = NLMSG_NEXT(header, remaining)) {
        switch (header->nlmsg_type) {
          case RTM_NEWLINK:
          case RTM_DELLINK:
          case RTM_NEWADDR:
          case RTM_DELADDR:
          case RTM_NEWROUTE:
          case RTM_DELROUTE:
          case RTM_NEWRULE:
          case RTM_DELRULE:
          case NLMSG_OVERRUN:
            g_route_sync_requested = true;
            break;
          default:
            break;
        }
      }
    }
  }
  close(fd);
}

void signalHandler(int) { g_running = false; }

std::string statusJson(const Config& config, int priority) {
  const bool nodeOnline = g_node_started && zts_node_is_online() == 1;
  const bool dataPlaneReady = nodeOnline && g_network_ready && g_tap_ready &&
                              g_route_sync_ok && g_frame_bridge_ready;
  std::ostringstream json;
  json << "{\"running\":" << (g_running ? "true" : "false")
       << ",\"nodeOnline\":" << (nodeOnline ? "true" : "false")
       << ",\"planetLoaded\":" << (g_planet_loaded ? "true" : "false")
       << ",\"activationId\":\"" << jsonEscape(g_activation_id) << "\""
       << ",\"networkReady\":" << (g_network_ready ? "true" : "false")
       << ",\"tapReady\":" << (g_tap_ready ? "true" : "false")
       << ",\"routeSync\":" << (g_route_sync_ok ? "true" : "false")
       << ",\"dataPlaneReady\":" << (dataPlaneReady ? "true" : "false")
       << ",\"networkId\":\"" << config.network_id << "\""
       << ",\"interface\":\"" << config.interface_name << "\""
       << ",\"rulePriority\":" << priority
       << ",\"multicastMembers\":" << g_multicast_member_count.load()
       << ",\"multicastSync\":" << (g_multicast_sync_ok.load() ? "true" : "false")
       << ",\"frameBridge\":" << (g_frame_bridge_ready ? "true" : "false")
       << ",\"lastError\":\"" << jsonEscape(lastError()) << "\"}";
  return json.str();
}

void controlLoop(const Config& config, int priority) {
  unlink(g_control_path.c_str());
  const int server = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (server < 0) {
    setLastError("unable to create control socket");
    return;
  }
  struct sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", g_control_path.c_str());
  if (bind(server, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 || listen(server, 4) < 0) {
    setLastError("unable to bind control socket");
    close(server);
    return;
  }
  chmod(g_control_path.c_str(), 0600);
  while (g_running) {
    struct pollfd descriptor{server, POLLIN, 0};
    const int ready = poll(&descriptor, 1, 500);
    if (ready < 0) { if (errno == EINTR) continue; break; }
    if (ready == 0 || !(descriptor.revents & POLLIN)) continue;
    const int client = accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0) { if (errno == EINTR) continue; break; }
    const timeval timeout{2, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    char command[64] = {};
    const auto size = read(client, command, sizeof(command) - 1);
    const std::string value = trim(size > 0 ? std::string(command, command + size) : "status");
    std::string response;
    if (value == "stop") { g_running = false; response = "stopping\n"; }
    else if (value == "routes") {
      response = "table=" + std::to_string(config.table) + " priority=" + std::to_string(priority) + "\n";
    } else if (value == "multicast") {
      response = "{\"members\":" + std::to_string(g_multicast_member_count.load()) +
          ",\"synchronized\":" + (g_multicast_sync_ok.load() ? std::string("true") : std::string("false")) + "}\n";
    } else response = statusJson(config, priority) + "\n";
    write(client, response.data(), response.size());
    close(client);
  }
  close(server);
  unlink(g_control_path.c_str());
}

int sendControl(const std::string& command) {
  const int client = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
  if (client < 0) return 1;
  const timeval timeout{2, 0};
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
  struct sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::snprintf(address.sun_path, sizeof(address.sun_path), "%s", g_control_path.c_str());
  if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
    close(client);
    return 1;
  }
  write(client, command.data(), command.size());
  char response[4096] = {};
  ssize_t size;
  bool received = false;
  while ((size = read(client, response, sizeof(response))) > 0) {
    received = true;
    std::cout.write(response, size);
  }
  close(client);
  return received && size == 0 ? 0 : 1;
}

bool acquireInstanceLock() {
  mkdir("/data/adb/zt-global", 0700);
  mkdir("/data/adb/zt-global/runtime", 0700);
  g_instance_lock_fd = open(kLockPath, O_CREAT | O_RDWR | O_CLOEXEC, 0600);
  if (g_instance_lock_fd < 0 || flock(g_instance_lock_fd, LOCK_EX | LOCK_NB) < 0) {
    if (g_instance_lock_fd >= 0) close(g_instance_lock_fd);
    g_instance_lock_fd = -1;
    logLine("another zt-globald instance is already running");
    return false;
  }
  std::ofstream pid(kPidPath, std::ios::trunc);
  if (!pid) {
    logLine("unable to write daemon pid file");
    close(g_instance_lock_fd);
    g_instance_lock_fd = -1;
    return false;
  }
  pid << getpid() << '\n';
  chmod(kPidPath, 0600);
  return true;
}

void releaseInstanceLock() {
  unlink(kPidPath);
  if (g_instance_lock_fd >= 0) {
    flock(g_instance_lock_fd, LOCK_UN);
    close(g_instance_lock_fd);
    g_instance_lock_fd = -1;
  }
}

}  // namespace

int main(int argc, char** argv) {
  // Keep the flock in a parent process, so it is not inherited by a daemon
  // launched by the shell transaction. Kernel releases it even on a crash.
  if (argc >= 5 && std::string(argv[1]) == "--with-lock") {
    const int fd = open(argv[2], O_CREAT | O_RDWR | O_CLOEXEC, 0600);
    if (fd < 0 || flock(fd, LOCK_EX | LOCK_NB) != 0) {
      std::cout << "{\"ok\":false,\"error\":\"operation_in_progress\"}" << std::endl;
      if (fd >= 0) close(fd);
      return 2;
    }
    const pid_t child = fork();
    if (child == 0) { close(fd); execvp(argv[3], argv + 3); _exit(127); }
    int result = 0;
    pid_t waited = -1;
    if (child > 0) do { waited = waitpid(child, &result, 0); } while (waited < 0 && errno == EINTR);
    close(fd);
    return waited > 0 && WIFEXITED(result) ? WEXITSTATUS(result) : 2;
  }
  std::string configPath = "/data/adb/zt-global/config.ini";
  bool control = false;
  std::string controlCommand = "status";
  std::string planetToValidate;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "--config" && index + 1 < argc) configPath = argv[++index];
    else if (arg == "--control") { control = true; if (index + 1 < argc) controlCommand = argv[++index]; }
    else if (arg == "--validate-planet" && index + 1 < argc) planetToValidate = argv[++index];
    else if (arg == "--activation-id" && index + 1 < argc) g_activation_id = argv[++index];
  }
  if (!planetToValidate.empty()) {
    std::string reason;
    if (!validatePlanetFile(planetToValidate, &reason)) {
      std::cerr << reason << std::endl;
      return 2;
    }
    std::cout << "valid planet" << std::endl;
    return 0;
  }
  if (control) return sendControl(controlCommand);

  if (!acquireInstanceLock()) return 10;

  Config config;
  if (!loadConfig(configPath, &config)) { logLine("unable to read " + configPath); releaseInstanceLock(); return 2; }
  cleanupOwnedRoutingState(true);
  cleanupLegacyModuleRoutes(config);
  if (!config.enabled) { logLine("module disabled"); releaseInstanceLock(); return 0; }
  if (!config.network_id.empty()) {
    g_network_id = parseNetworkId(config.network_id);
    if (g_network_id == 0) { logLine("invalid network_id"); releaseInstanceLock(); return 2; }
  }
  signal(SIGTERM, signalHandler);
  signal(SIGINT, signalHandler);
  signal(SIGPIPE, SIG_IGN);

  if (!prepareIdentity(config)) { releaseInstanceLock(); return 3; }
  if (!config.storage_path.empty() && zts_init_from_storage(config.storage_path.c_str()) < 0) {
    logLine("zts_init_from_storage failed"); releaseInstanceLock(); return 3;
  }
  // A manually installed planet is authoritative. Keep roots_path as a
  // compatibility fallback for existing installations that only provide the
  // libzt roots file.
  std::string rootsSource;
  std::string rootsBytes;
  for (const auto& candidate : {config.planet_path, config.roots_path}) {
    if (candidate.empty()) continue;
    struct stat info{};
    if (lstat(candidate.c_str(), &info) != 0 && errno == ENOENT) continue;
    std::string reason;
    if (!validatePlanetFile(candidate, &reason)) {
      logLine("Planet load failed: " + candidate + ": " + reason);
      releaseInstanceLock(); return 3;
    }
    std::ifstream input(candidate, std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    if (bytes.empty()) { logLine("Planet read failed"); releaseInstanceLock(); return 3; }
    rootsSource = candidate;
    rootsBytes = std::move(bytes);
    break;
  }
  if (!rootsBytes.empty()) {
    if (zts_init_set_roots(rootsBytes.data(), static_cast<unsigned int>(rootsBytes.size())) < 0) {
      logLine("zts_init_set_roots failed for " + rootsSource);
      releaseInstanceLock(); return 3;
    }
    logLine("submitted custom planet/roots from " + rootsSource);
  } else if (unlink((config.storage_path + "/roots").c_str()) != 0 && errno != ENOENT) {
    logLine("unable to remove cached Planet for official startup");
    releaseInstanceLock(); return 3;
  }
  if (config.port > 0) zts_init_set_port(static_cast<unsigned short>(config.port));
  if (zts_node_start() < 0) { logLine("zts_node_start failed"); releaseInstanceLock(); return 3; }
  g_node_started = true;
  const int priority = choosePriority(config);
  const bool frameAdapterAvailable = zts_net_set_frame_callback && zts_net_send_frame;
  std::thread controlThread(controlLoop, config, priority);
  // Local initialization only: neither internet reachability nor controller
  // authorization participates in Planet activation.
  for (int i = 0; i < 300 && g_running; ++i) {
    char loaded[kMaxPlanetSize];
    unsigned int length = sizeof(loaded);
    if (zts_node_get_loaded_planet(loaded, &length) == ZTS_ERR_OK) {
      if (!rootsBytes.empty() && rootsBytes != std::string(loaded, length)) {
        setLastError("Core did not load the requested Planet");
        logLine(lastError());
        break;
      }
      g_planet_loaded = true;
      logLine("Planet loaded by Core: " + (rootsSource.empty() ? std::string("official") : rootsSource));
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!g_planet_loaded || !g_running) {
    logLine("local Planet initialization failed or interrupted");
    g_running = false;
    if (controlThread.joinable()) controlThread.join();
    zts_node_stop(); g_node_started = false; releaseInstanceLock(); return 3;
  }
  std::thread tapThread;
  std::thread multicastThread;
  std::thread netlinkThread(netlinkRouteLoop);
  std::string appliedFingerprint;
  std::vector<std::pair<bool, std::string>> cachedUnderlay;
  bool identityBackedUp = backupIdentity(config);
  bool joined = false;
  auto lastSetupAttempt = std::chrono::steady_clock::now() - std::chrono::seconds(5);
  auto lastLibztSnapshot = std::chrono::steady_clock::now();
  auto lastFallbackVerify = lastLibztSnapshot;
  auto lastRouteSync = lastLibztSnapshot;
  while (g_running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    const auto now = std::chrono::steady_clock::now();
    if (g_network_id == 0) {
      setLastError("Network ID is not configured");
      continue;
    }
    const bool setupDue = now - lastSetupAttempt >= std::chrono::seconds(5);
    if (!joined && setupDue) {
      lastSetupAttempt = now;
      joined = zts_net_join(g_network_id) == ZTS_ERR_OK;
      if (!joined) setLastError("ZeroTier network join pending; retrying");
    }
    const bool nodeOnline = zts_node_is_online() == 1;
    const bool transportReady = joined && zts_net_transport_is_ready(g_network_id) == 1;
    if (!nodeOnline || !transportReady) {
      if (g_route_sync_ok || !appliedFingerprint.empty()) cleanupOwnedRoutingState(true);
      appliedFingerprint.clear();
      g_network_ready = false;
      g_route_sync_ok = false;
      if (!nodeOnline) setLastError("ZeroTier node offline; waiting for connectivity");
      else if (zts_net_get_status(g_network_id) == ZTS_NETWORK_STATUS_ACCESS_DENIED)
        setLastError("Waiting for controller authorization");
      else setLastError("Waiting for ZeroTier network configuration");
      continue;
    }
    if ((!g_tap_ready || !g_frame_bridge_ready) && setupDue) {
      lastSetupAttempt = now;
      if (!frameAdapterAvailable) {
        setLastError("libzt frame bridge symbols are unavailable");
        continue;
      }
      if (!g_frame_bridge_ready) {
        g_frame_bridge_ready = zts_net_set_frame_callback(g_network_id, frameFromLibzt) == ZTS_ERR_OK;
        if (!g_frame_bridge_ready) { setLastError("unable to register libzt frame callback"); continue; }
      }
      if (!g_tap_ready) {
        g_tap_fd = openTap(config);
        g_tap_ready = g_tap_fd >= 0;
        if (!g_tap_ready) { setLastError("unable to create or configure TAP interface"); continue; }
        tapThread = std::thread(tapLoop);
        multicastThread = std::thread(multicastLoop, config);
      }
      g_route_sync_requested = true;
    }
    if (!g_route_sync_ok && now - lastRouteSync >= std::chrono::seconds(5))
      g_route_sync_requested = true;
    const bool netlinkRequested = g_route_sync_requested.exchange(false);
    const bool libztDue = now - lastLibztSnapshot >= kLibztSnapshotInterval;
    const bool fallbackDue = now - lastFallbackVerify >= kFallbackVerifyInterval;
    if (g_tap_ready && g_frame_bridge_ready &&
        (netlinkRequested || libztDue || fallbackDue) && now - lastRouteSync >= std::chrono::milliseconds(500)) {
      const bool refreshUnderlay = netlinkRequested || fallbackDue;
      synchronizeRouting(config, priority, &appliedFingerprint, &cachedUnderlay,
                         refreshUnderlay, netlinkRequested || fallbackDue);
      lastRouteSync = now;
      if (libztDue) lastLibztSnapshot = now;
      if (fallbackDue) lastFallbackVerify = now;
      if (!identityBackedUp && g_route_sync_ok && g_network_ready)
        identityBackedUp = backupIdentity(config);
    }
  }

  if (g_tap_fd >= 0) { close(g_tap_fd); g_tap_fd = -1; }
  if (tapThread.joinable()) tapThread.join();
  if (multicastThread.joinable()) multicastThread.join();
  if (netlinkThread.joinable()) netlinkThread.join();
  if (controlThread.joinable()) controlThread.join();
  clearRoutes(config);
  g_tap_ready = false;
  g_network_ready = false;
  g_route_sync_ok = false;
  g_frame_bridge_ready = false;
  if (joined) zts_net_leave(g_network_id);
  zts_node_stop();
  g_node_started = false;
  releaseInstanceLock();
  return 0;
}
