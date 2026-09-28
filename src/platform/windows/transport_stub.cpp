#include "internal/transport.hpp"

namespace cyberpower::platform {

// Windows port is intentionally a stub. The recovered protocol (CR-framed
// text, v3 CRC-8, HID usage pairs) does not depend on macOS; a later port
// would open the same vendor id through SetupAPI / HidD_GetAttributes and
// the same 2400 8N1 line settings through the Win32 COMM API.

std::vector<DeviceInfo> list_hid() { return {}; }

std::vector<DeviceInfo> list_serial() { return {}; }

std::unique_ptr<Transport> open_hid(const DeviceInfo&, std::string& error) {
  error = "Windows HID transport is not implemented";
  return nullptr;
}

std::unique_ptr<Transport> open_serial(const DeviceInfo&, std::string& error) {
  error = "Windows serial transport is not implemented";
  return nullptr;
}

}  // namespace cyberpower::platform
