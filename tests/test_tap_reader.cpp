#include "tap_reader.h"
#include <cassert>
#include <chrono>
#include <fcntl.h>
#include <future>
#include <iostream>
#include <sys/socket.h>
#include <thread>

int main() {
  using namespace zt_tap;
  assert(classifyRead(-1, EINTR) == ReadResult::Retry);
  assert(classifyRead(-1, EAGAIN) == ReadResult::Retry);
  assert(classifyRead(-1, EWOULDBLOCK) == ReadResult::Retry);
  assert(classifyRead(-1, EBADF) == ReadResult::Error);
  assert(classifyRead(0, EINTR) == ReadResult::End);
  assert(classifyRead(10, EIO) == ReadResult::Frame);
  int pair[2];
  assert(socketpair(AF_UNIX, SOCK_DGRAM, 0, pair) == 0);
  assert(fcntl(pair[0], F_SETFL, O_NONBLOCK) == 0);
  std::atomic<bool> running{true};
  std::promise<void> delivered;
  auto notification = delivered.get_future();
  std::string error;
  std::thread reader([&] {
    error = readFrames(pair[0], running, [&](const void* frame, unsigned int size) {
      assert(size == 4 && std::memcmp(frame, "test", 4) == 0);
      delivered.set_value();
    });
  });
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  const auto sent = std::chrono::steady_clock::now();
  assert(write(pair[1], "test", 4) == 4);
  assert(notification.wait_for(std::chrono::milliseconds(200)) == std::future_status::ready);
  std::cout << "poll packet wakeup: " << std::chrono::duration_cast<std::chrono::microseconds>(
    std::chrono::steady_clock::now() - sent).count() << " us\n";
  running = false;
  reader.join();
  assert(error.empty());
  running = true;
  // A zero-length datagram exercises read()==0, independently of errno.
  assert(write(pair[1], "", 0) == 0);
  error = readFrames(pair[0], running, [](const void*, unsigned int) { assert(false); });
  assert(error.find("zero bytes") != std::string::npos);
  const int invalid = pair[0];
  close(pair[0]);
  close(pair[1]);
  error = readFrames(invalid, running, [](const void*, unsigned int) { assert(false); });
  assert(error.find("invalid/closed/error") != std::string::npos);
  std::cout << "TAP reader idle/wakeup/stop/EOF/error tests passed\n";
}
