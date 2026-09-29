#pragma once

#include <string>

namespace cyberpower::platform {

// The first group is explicitly named by the recovered driver. The generic
// macOS names are only eligible when the caller deliberately opts in.
bool serial_port_name_is_candidate(const std::string& name, bool include_generic);

}  // namespace cyberpower::platform
