#pragma once
#include <atomic>
#include <cerrno>
#include <cstring>
#include <poll.h>
#include <string>
#include <unistd.h>
#include <vector>

namespace zt_tap {
enum class ReadResult { Frame, Retry, End, Error };
inline ReadResult classifyRead(ssize_t count, int error) {
  if (count > 0) return ReadResult::Frame;
  if (count == 0) return ReadResult::End; // errno is undefined on EOF.
  if (error == EINTR || error == EAGAIN || error == EWOULDBLOCK) return ReadResult::Retry;
  return ReadResult::Error;
}

// fd must be nonblocking and remain open until this function returns.
// Timeout only bounds shutdown latency; readable packets wake poll immediately.
template<class Deliver>
std::string readFrames(int fd, const std::atomic<bool>& running, Deliver deliver) {
  std::vector<unsigned char> frame(4096);
  while (running.load()) {
    pollfd descriptor{fd, POLLIN, 0};
    const int ready = poll(&descriptor, 1, 250);
    if (!running.load()) return {};
    if (ready < 0) {
      const int error = errno;
      if (error == EINTR) continue;
      return "TAP poll failed: " + std::string(std::strerror(error));
    }
    if (ready == 0) continue;
    if (descriptor.revents & (POLLNVAL | POLLHUP | POLLERR))
      return "TAP poll reported invalid/closed/error descriptor";
    if (!(descriptor.revents & POLLIN)) continue;
    const auto received = read(fd, frame.data(), frame.size());
    const int error = received < 0 ? errno : 0;
    if (!running.load()) return {};
    switch (classifyRead(received, error)) {
      case ReadResult::Frame: deliver(frame.data(), static_cast<unsigned int>(received)); break;
      case ReadResult::Retry: break;
      case ReadResult::End: return "TAP read returned zero bytes";
      case ReadResult::Error: return "TAP read failed: " + std::string(std::strerror(error));
    }
  }
  return {};
}
} // namespace zt_tap
