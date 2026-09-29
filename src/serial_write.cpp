#include "internal/serial_write.hpp"

#include <cerrno>
#include <chrono>
#include <poll.h>
#include <unistd.h>

namespace cyberpower::platform {

Error write_all_nonblocking(int fd, const char* data, std::size_t size, int timeout_ms) {
  std::size_t written = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  while (written < size) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return Error::Timeout;
    const ssize_t rc = ::write(fd, data + written, size - written);
    if (rc > 0) {
      written += static_cast<std::size_t>(rc);
      continue;
    }
    if (rc < 0 && errno == EINTR) continue;
    if (rc < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return Error::Io;
    pollfd pfd{};
    pfd.fd = fd;
    pfd.events = POLLOUT;
    constexpr int kWaitSliceMs = 100;
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now).count();
    const int wait = static_cast<int>(remaining < kWaitSliceMs ? remaining : kWaitSliceMs);
    const int polled = ::poll(&pfd, 1, wait);
    if (polled < 0) {
      if (errno == EINTR) continue;
      return Error::Io;
    }
    if (polled > 0 && (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) return Error::Io;
  }
  return Error::Ok;
}

}  // namespace cyberpower::platform
