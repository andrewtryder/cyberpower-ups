#pragma once

#include "cyberpower/errors.hpp"
#include "cyberpower/protocol.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cyberpower {

enum class TransportKind {
  Hid,     // IOHID, vendor 0x0764
  Serial,  // termios, default 2400 8N1
};

struct DeviceInfo {
  TransportKind transport = TransportKind::Hid;
  std::string path;          // IORegistry path or /dev/cu.* node
  std::string product;       // USB product string when the OS provides one
  std::string serial_number;
  uint16_t vendor_id = 0;
  uint16_t product_id = 0;
  int location_id = 0;
};

// Snapshot in engineering units. A field is present only when the transport
// actually returned it. Serial values come from the v2e "D" frame; HID
// values come from the USB HID Power Device usages recovered in the driver.
struct Status {
  bool ok = false;
  Error error = Error::Ok;
  std::string message;
  TransportKind transport = TransportKind::Hid;

  // Unparsed status line for serial. Empty for HID.
  std::string raw;

  std::optional<double> battery_percent;
  std::optional<double> input_voltage_v;
  std::optional<double> output_voltage_v;
  std::optional<double> load_percent;
  std::optional<double> runtime_seconds;
  std::optional<double> frequency_hz;
  std::optional<double> temperature_c;
  std::optional<double> battery_voltage_v;

  std::optional<bool> ac_present;
  std::optional<bool> charging;
  std::optional<bool> discharging;

  // Every tag from a serial status frame, in driver stored units.
  protocol::StatusFrame frame;
};

// One open UPS. Platform handles live in the .cpp files; this type does
// not mention IOKit or Win32.
class Ups {
 public:
  Ups(Ups&&) noexcept;
  Ups& operator=(Ups&&) noexcept;
  ~Ups();

  Ups(const Ups&) = delete;
  Ups& operator=(const Ups&) = delete;

  const DeviceInfo& info() const;

  // HID: read Power Device / Battery usages.
  // Serial: write "D\r" and parse the '#' frame.
  Status read_status();

  // Serial only. `command` may already end in CR; otherwise CR is appended
  // (v2e delimiter is 0x0D). HID devices return Error::NotSupported.
  // The returned string includes the trailing CR when the device sent one.
  Error transact(const std::string& command, std::string& response);

 private:
  struct Impl;
  explicit Ups(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
  friend std::vector<DeviceInfo> list_devices();
  friend std::optional<Ups> open_device(const DeviceInfo& info, std::string* error);
};

// Enumerate CyberPower HID devices (vendor 0x0764) and macOS cu.* serial
// nodes whose names match the driver's port filters.
std::vector<DeviceInfo> list_devices();

std::optional<Ups> open_device(const DeviceInfo& info, std::string* error);

}  // namespace cyberpower
