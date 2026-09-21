#include <ZeroTierSockets.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

bool sendAllLocal(int fd, const char* data, size_t size) {
  while (size > 0) {
    const auto sent = send(fd, data, size, 0);
    if (sent <= 0) return false;
    data += sent;
    size -= static_cast<size_t>(sent);
  }
  return true;
}

bool sendAllZt(int fd, const char* data, size_t size) {
  while (size > 0) {
    const auto sent = zts_bsd_send(fd, data, size, 0);
    if (sent <= 0) return false;
    data += sent;
    size -= static_cast<size_t>(sent);
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc != 6) {
    std::cerr << "usage: zt_adb_forward <storage> <roots> <network-id> <zt-host> <local-port>\n";
    return 2;
  }
  const std::string storage = argv[1];
  const std::string rootsPath = argv[2];
  const uint64_t networkId = std::strtoull(argv[3], nullptr, 16);
  const std::string remoteHost = argv[4];
  const int localPort = std::stoi(argv[5]);

  if (zts_init_from_storage(storage.c_str()) < 0) return 3;
  std::ifstream roots(rootsPath, std::ios::binary);
  std::vector<char> rootsData((std::istreambuf_iterator<char>(roots)), {});
  if (!rootsData.empty() && zts_init_set_roots(rootsData.data(), rootsData.size()) < 0) return 4;
  if (zts_node_start() < 0) return 5;
  for (int i = 0; i < 90 && zts_node_is_online() != 1; ++i)
    std::this_thread::sleep_for(std::chrono::seconds(1));
  if (zts_net_join(networkId) < 0) return 6;
  for (int i = 0; i < 90 && zts_addr_is_assigned(networkId, ZTS_AF_INET) != 1; ++i)
    std::this_thread::sleep_for(std::chrono::seconds(1));

  const int server = socket(AF_INET, SOCK_STREAM, 0);
  int one = 1;
  setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
  sockaddr_in local{};
  local.sin_family = AF_INET;
  local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  local.sin_port = htons(static_cast<uint16_t>(localPort));
  if (bind(server, reinterpret_cast<sockaddr*>(&local), sizeof(local)) < 0 || listen(server, 2) < 0) return 7;
  std::cerr << "ready 127.0.0.1:" << localPort << " -> " << remoteHost << ":5555\n";

  while (true) {
    const int client = accept(server, nullptr, nullptr);
    if (client < 0) continue;
    const int zt = zts_bsd_socket(ZTS_AF_INET, ZTS_SOCK_STREAM, ZTS_IPPROTO_TCP);
    zts_sockaddr_in remote{};
    zts_socklen_t remoteLen = sizeof(remote);
    if (zt < 0 || zts_util_ipstr_to_saddr(remoteHost.c_str(), 5555,
        reinterpret_cast<zts_sockaddr*>(&remote), &remoteLen) < 0 ||
        zts_bsd_connect(zt, reinterpret_cast<zts_sockaddr*>(&remote), sizeof(remote)) < 0) {
      close(client);
      if (zt >= 0) zts_bsd_close(zt);
      continue;
    }
    std::atomic<bool> open{true};
    std::thread up([&] {
      char buffer[16384];
      while (open) {
        const auto size = recv(client, buffer, sizeof(buffer), 0);
        if (size <= 0 || !sendAllZt(zt, buffer, static_cast<size_t>(size))) break;
      }
      open = false;
      zts_bsd_shutdown(zt, ZTS_SHUT_WR);
    });
    std::thread down([&] {
      char buffer[16384];
      while (open) {
        const auto size = zts_bsd_recv(zt, buffer, sizeof(buffer), 0);
        if (size <= 0 || !sendAllLocal(client, buffer, static_cast<size_t>(size))) break;
      }
      open = false;
      shutdown(client, SHUT_WR);
    });
    up.join();
    down.join();
    close(client);
    zts_bsd_close(zt);
  }
}
