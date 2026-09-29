#include <cyberpower/ups.hpp>

int main() {
  const auto devices = cyberpower::list_devices();
  return devices.empty() ? 0 : 0;
}
