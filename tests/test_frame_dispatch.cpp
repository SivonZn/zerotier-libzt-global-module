#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <vector>
#define ZTS_API
#define ZTCALL
#define ZTS_EXTERNAL_TAP_ONLY 1
constexpr int ZTS_ERR_ARG = -3, ZTS_ERR_OK = 0;
constexpr signed char ERR_IF = -12;
using zts_vnet_frame_callback = void (*)(uint64_t, const void*, unsigned int);
struct Mutex { std::mutex mutex; struct Lock {
  std::lock_guard<std::mutex> lock;
  explicit Lock(Mutex& m): lock(m.mutex) {}
}; };
struct MAC {
  uint8_t bytes[6];
  void copyTo(void* out, unsigned int n) const { memcpy(out, bytes, n); }
};
struct VirtualTap {
  bool _enabled = true;
  uint64_t _net_id = 1;
  void put(const MAC&, const MAC&, unsigned int, const void*, unsigned int);
};
struct netif {};
struct pbuf {};
#include "frame-dispatch.inc"
static int count;
static uint64_t network;
static std::vector<uint8_t> packet;
static void capture(uint64_t id, const void* data, unsigned int len) {
  ++count; network = id;
  const auto* bytes = static_cast<const uint8_t*>(data);
  packet.assign(bytes, bytes + len);
}
int main() {
  VirtualTap tap;
  MAC src{{1,2,3,4,5,6}}, dst{{6,5,4,3,2,1}};
  uint8_t payload[]{11,22,33};
  assert(zts_net_set_frame_callback(0, capture) == ZTS_ERR_ARG);
  assert(zts_net_set_frame_callback(1, capture) == ZTS_ERR_OK);
  for (auto type : {0x0800u, 0x0806u, 0x86ddu}) {
    const auto before = count;
    tap.put(src, dst, type, payload, sizeof(payload));
    assert(count == before + 1 && network == 1 && packet.size() == 17);
    assert(memcmp(packet.data(), dst.bytes, 6) == 0);
    assert(memcmp(packet.data()+6, src.bytes, 6) == 0);
    assert(packet[12] == (type >> 8) && packet[13] == (type & 255));
    assert(memcmp(packet.data()+14, payload, 3) == 0);
  }
  tap._net_id = 2;
  tap.put(src, dst, 0x0800, payload, 3);
  assert(count == 3);
  tap._net_id = 1;
  zts_net_set_frame_callback(1, nullptr);
  tap.put(src, dst, 0x0800, payload, 3);
  assert(count == 3);
  zts_net_set_frame_callback(1, capture);
  tap._enabled = false;
  tap.put(src, dst, 0x0800, payload, 3);
  assert(count == 3);
  assert(zts_lwip_eth_tx(nullptr, nullptr) == ERR_IF);
}
