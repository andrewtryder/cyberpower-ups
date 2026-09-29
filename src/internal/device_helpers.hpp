#pragma once

#include "internal/transport.hpp"

namespace cyberpower::internal {

// Shared with offline tests so fallback policy is verified without hardware.
Error toggle_buzzer(platform::Transport& transport);

}  // namespace cyberpower::internal
