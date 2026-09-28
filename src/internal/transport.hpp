#pragma once

// Internal. Not part of the installed public API.

#include "cyberpower/ups.hpp"

#include <memory>
#include <string>
#include <vector>

namespace cyberpower::platform {

class Transport {
 public:
  virtual ~Transport() = default;
  virtual const DeviceInfo& info() const = 0;
  virtual Status read_status() = 0;
  virtual Error transact(const std::string& command, std::string& response) = 0;
};

std::vector<DeviceInfo> list_hid();
std::vector<DeviceInfo> list_serial();

std::unique_ptr<Transport> open_hid(const DeviceInfo& info, std::string& error);
std::unique_ptr<Transport> open_serial(const DeviceInfo& info, std::string& error);

}  // namespace cyberpower::platform
