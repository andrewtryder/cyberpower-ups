#include "cyberpower/ups.hpp"

#include "internal/transport.hpp"

namespace cyberpower {

std::vector<DeviceInfo> list_devices() {
  std::vector<DeviceInfo> all = platform::list_hid();
  std::vector<DeviceInfo> serial = platform::list_serial();
  all.insert(all.end(), serial.begin(), serial.end());
  return all;
}

}  // namespace cyberpower
