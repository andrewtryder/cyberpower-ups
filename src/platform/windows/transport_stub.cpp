#include "internal/transport.hpp"

// Windows transport skeleton.
//
// The recovered protocol (CR-framed text, v3 CRC-8, HID usage pairs) does not
// depend on macOS. Fill in the TODOs below with SetupAPI / HID and Win32 COMM
// code; keep the public C++/C APIs and protocol parsers unchanged.
//
// Suggested references once implementing:
//   - HidD_GetAttributes / SetupDiEnumDeviceInterfaces for vendor 0x0764
//   - CreateFile + SetCommState for 2400 8N1 (see protocol::kDefaultBaud)
//   - Mirror the macOS backends in src/platform/macos/ for read_status /
//     transact behavior

namespace cyberpower::platform {

namespace {

// TODO(windows-hid): enumerate CyberPower HID devices via SetupAPI.
// Match vendor_id == protocol::kVendorId (0x0764). Prefer product ids in
// protocol::kKnownProductIds when filtering is needed.
//
// Rough outline:
//   1. SetupDiGetClassDevs(GUID_DEVINTERFACE_HID, ...)
//   2. SetupDiEnumDeviceInterfaces / SetupDiGetDeviceInterfaceDetail
//   3. CreateFile(detail->DevicePath, ...) then HidD_GetAttributes
//   4. Fill DeviceInfo { transport=Hid, path=DevicePath, vendor/product ids }

// TODO(windows-serial): enumerate COM ports that look like UPS adapters.
// Win32: GetCommPorts / SetupDi for Ports class, or QueryDosDevice("COM*").
// Default line settings when opening: 2400 8N1, raw (no XON/XOFF).

}  // namespace

std::vector<DeviceInfo> list_hid() {
  // TODO(windows-hid): return discovered devices instead of empty.
  return {};
}

std::vector<DeviceInfo> list_serial() {
  // TODO(windows-serial): return discovered COM ports instead of empty.
  return {};
}

std::unique_ptr<Transport> open_hid(const DeviceInfo& /*info*/, std::string& error) {
  // TODO(windows-hid): implement a Transport that:
  //   - opens the HID handle from info.path
  //   - read_status() reads Power Device / Battery usages (see protocol::kStatusUsages)
  //   - transact() returns Error::NotSupported (HID path is usage-based)
  error = "Windows HID transport is not implemented";
  return nullptr;
}

std::unique_ptr<Transport> open_serial(const DeviceInfo& /*info*/, std::string& error) {
  // TODO(windows-serial): implement a Transport that:
  //   - CreateFile(info.path) + SetCommState(2400 8N1)
  //   - read_status() writes protocol::cmd::kStatus ("D\r") and parses '#' frame
  //   - transact() writes command + CR, reads until CR / timeout
  error = "Windows serial transport is not implemented";
  return nullptr;
}

}  // namespace cyberpower::platform
