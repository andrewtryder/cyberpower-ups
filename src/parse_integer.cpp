#include "internal/parse_integer.hpp"

#include <charconv>

namespace cyberpower::internal {

bool parse_int_strict(const std::string& text, int& value) {
  if (text.empty()) return false;
  const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
  return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

}  // namespace cyberpower::internal
