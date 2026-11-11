#include "internal/serial_discovery.hpp"

#include <string_view>

namespace cyberpower::platform {

bool serial_port_name_is_candidate(const std::string& name, bool include_generic) {
  std::string_view s = name;
  const auto slash = s.find_last_of('/');
  if (slash != std::string_view::npos) {
    s = s.substr(slash + 1);
  }

  // Driver-named devices explicitly identified by the vendor driver (CH340/wchusbserial).
  const bool driver_named = s.find("cu.wchusbserial") != std::string_view::npos ||
                            s.find("wchusbserial") != std::string_view::npos;
  if (driver_named) return true;
  if (!include_generic) return false;

  // Generic macOS USB-serial ports (require explicit CPUPS_INCLUDE_GENERIC_SERIAL=1).
  if (s.rfind("cu.usbserial", 0) == 0 || s.rfind("cu.usbmodem", 0) == 0 ||
      s.rfind("cu.SLAB", 0) == 0 || s.rfind("tty.usbserial", 0) == 0 ||
      s.rfind("tty.usbmodem", 0) == 0 || s.rfind("tty.SLAB", 0) == 0) {
    return true;
  }

  // Generic Linux serial ports (require explicit CPUPS_INCLUDE_GENERIC_SERIAL=1).
  if (s.rfind("ttyUSB", 0) == 0 || s.rfind("ttyACM", 0) == 0 || s.rfind("ttyS", 0) == 0) {
    return true;
  }

  return false;
}

}  // namespace cyberpower::platform
