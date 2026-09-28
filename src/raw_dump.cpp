#include "internal/transport.hpp"

namespace cyberpower::platform {

std::ostream*& raw_dump_sink() {
  static std::ostream* sink = nullptr;
  return sink;
}

void dump_raw_line(const std::string& line) {
  if (std::ostream* sink = raw_dump_sink()) {
    *sink << line << '\n';
  }
}

}  // namespace cyberpower::platform

namespace cyberpower {

void set_raw_dump_sink(std::ostream* out) { platform::raw_dump_sink() = out; }

}  // namespace cyberpower
