#pragma once

#include "cyberpower/errors.hpp"

#include <cstddef>

namespace cyberpower::platform {

// Write a complete nonblocking buffer, waiting for POLLOUT as needed. timeout
// bounds the whole operation, including interrupted/partial writes.
Error write_all_nonblocking(int fd, const char* data, std::size_t size, int timeout_ms);

}  // namespace cyberpower::platform
