#pragma once

#include <string>

namespace cyberpower::internal {

bool parse_int_strict(const std::string& text, int& value);

}  // namespace cyberpower::internal
