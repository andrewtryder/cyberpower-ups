#include "internal/serial_discovery.hpp"

namespace cyberpower::platform {

bool serial_port_name_is_candidate(const std::string& name, bool include_generic) {
  const bool driver_named = name.find("cu.wchusbserial") != std::string::npos ||
                            name.find("ttyUSB") != std::string::npos ||
                            name.find("ttyS") != std::string::npos;
  if (driver_named) return true;
  if (!include_generic) return false;
  return name.find("cu.usbserial") != std::string::npos ||
         name.find("cu.usbmodem") != std::string::npos || name.find("cu.SLAB") != std::string::npos;
}

}  // namespace cyberpower::platform
