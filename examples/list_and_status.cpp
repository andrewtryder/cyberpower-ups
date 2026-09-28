#include "cyberpower/protocol.hpp"
#include "cyberpower/ups.hpp"

#include <iostream>
#include <string>

// Thin library example. Prefer the `cpups` CLI in tools/ for day-to-day use.

namespace {

const char* transport_name(cyberpower::TransportKind kind) {
  return kind == cyberpower::TransportKind::Serial ? "serial" : "hid";
}

}  // namespace

int main() {
  std::string detail;
  if (!cyberpower::protocol::self_test(&detail)) {
    std::cerr << "protocol self-test failed: " << detail << "\n";
    return 1;
  }

  const auto devices = cyberpower::list_devices();
  std::cout << "devices: " << devices.size() << "\n";
  for (const cyberpower::DeviceInfo& device : devices) {
    std::cout << transport_name(device.transport) << " " << device.path;
    if (!device.product.empty()) std::cout << "  " << device.product;
    std::cout << "\n";

    std::string error;
    auto ups = cyberpower::open_device(device, &error);
    if (!ups) {
      std::cout << "  open failed: " << error << "\n";
      continue;
    }
    const cyberpower::Status status = ups->read_status();
    std::cout << "  ok=" << (status.ok ? "yes" : "no");
    if (status.battery_percent) std::cout << " battery=" << *status.battery_percent << "%";
    if (status.input_voltage_v) std::cout << " in=" << *status.input_voltage_v << "V";
    if (status.load_percent) std::cout << " load=" << *status.load_percent << "%";
    std::cout << "\n";
  }
  return 0;
}
