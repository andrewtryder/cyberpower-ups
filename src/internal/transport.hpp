#pragma once

// Internal. Not part of the installed public API.

#include "cyberpower/ups.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace cyberpower::platform {

// Optional sink for transport-level capture (serial TX/RX, HID input reports).
// Not thread-safe; intended for CLI --dump-raw. nullptr disables dumping.
std::ostream*& raw_dump_sink();
void dump_raw_line(const std::string& line);

class Transport {
 public:
  virtual ~Transport() = default;
  virtual const DeviceInfo& info() const = 0;
  virtual Status read_status() = 0;
  virtual Error transact(const std::string& command, std::string& response) = 0;

  // HID usage writes (Power Device 0x84/0x5A and 0x84/0x58). Serial returns
  // NotSupported so the Ups layer can fall back to text commands.
  virtual Error set_alarm_control(int /*value*/) { return Error::NotSupported; }
  virtual Error get_alarm_control(int& /*value*/) { return Error::NotSupported; }
  virtual Error set_test_mode(int /*value*/) { return Error::NotSupported; }
};

std::vector<DeviceInfo> list_hid();
std::vector<DeviceInfo> list_serial();

std::unique_ptr<Transport> open_hid(const DeviceInfo& info, std::string& error);
std::unique_ptr<Transport> open_serial(const DeviceInfo& info, std::string& error);

}  // namespace cyberpower::platform
