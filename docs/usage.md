# CLI and library usage

For a short overview, installation, and hardware compatibility, see the [README](../README.md) and [Platforms and hardware](platforms.md).

## CLI: status and monitoring

```bash
./build/cpups                       # Discover devices and read status
./build/cpups --json                # JSON output
./build/cpups --monitor             # Poll until Ctrl-C
./build/cpups --monitor --interval 1000
./build/cpups --monitor --json      # JSON lines on change
./build/cpups --monitor --every-poll
./build/cpups --self-test           # Offline parser/CRC test; no UPS
```

The smaller `examples/list_and_status.cpp` is provided as a library sample. Windows has a transport stub but supports portable offline tests.

## Commands that can change UPS behavior

**Safety warning:** Self-tests, calibration, buzzer changes, and scheduled operations can affect a UPS or its attached load. Avoid unverified commands on equipment supplying critical systems.

```bash
./build/cpups --test                # Quick test
./build/cpups --cancel-test
./build/cpups --buzzer              # Toggle alarm
./build/cpups --mute                # HID mute
./build/cpups --enable-alarm
./build/cpups --disable-alarm
./build/cpups --calibrate
./build/cpups --rating              # Serial only
./build/cpups --cancel-schedule     # Serial only
./build/cpups --indicator-test      # Serial only
./build/cpups --buzzer-test         # Serial only
./build/cpups --rating --json
```

| CLI / C++ method | HID | Serial |
| --- | --- | --- |
| `--test` / `self_test()` | Test(1) Feature write | `T\r` |
| `--cancel-test` / `cancel_test()` | Test(3) | `CT\r` |
| `--buzzer` / `toggle_buzzer()` | Alarm toggle/mute policy | `B\r` |
| `--mute` / `--enable-alarm` / `--disable-alarm` | Alarm values 3 / 2 / 1 | Not supported |
| `--calibrate` / `calibrate()` | Test(2), deep test | `TL\r` |
| `--rating`, `--cancel-schedule`, `--indicator-test`, `--buzzer-test` | Not supported | `F\r`, `C\r`, `TI\r`, `TB\r` |

For the CP1500PFCLCDa (VID:PID `0764:0601`), recovered HID Feature IDs are `0x0C` (alarm disable/enable/mute = 1/2/3) and `0x14` (quick/deep/abort test = 1/2/3). IDs and report layouts are **model-specific**, and Linux control writes have not yet been physically verified. See [Protocol notes](../PROTOCOL.md).

### Raw diagnostics

`--dump-raw` writes HID and serial captures to **stderr** so stdout remains JSON-clean:

```bash
./build/cpups --dump-raw
./build/cpups --json --dump-raw 2> capture.txt
```

Sanitize device serial numbers and host identifiers before sharing. See [fixtures](../fixtures/README.md) and [CONTRIBUTING.md](../CONTRIBUTING.md). Generic serial ports require [explicit opt-in](platforms.md#serial-discovery-and-settings).

## Use the library

### CMake and pkg-config

Link the installed package with:

```cmake
find_package(cyberpower-ups REQUIRED)
target_link_libraries(myapp PRIVATE cyberpower-ups::cyberpower-ups)
```

Supply `-DCMAKE_PREFIX_PATH=/path/to/prefix` if required. For pkg-config:

```bash
pkg-config --cflags --libs cyberpower-ups
```

The repository contains downstream examples under `tests/consumer-cmake/` and `tests/consumer-pkgconfig/`.

### C++ example

```cpp
#include <cyberpower/ups.hpp>
#include <iostream>

int main() {
  const auto devices = cyberpower::list_devices();
  if (devices.empty()) {
    std::cerr << "No UPS found\n";
    return 1;
  }

  auto ups = cyberpower::open_device(devices.front());
  if (!ups) {
    std::cerr << "Cannot open UPS\n";
    return 1;
  }

  const auto status = ups->read_status();
  if (!status.ok) {
    std::cerr << "Read failed: " << status.message << "\n";
    return 1;
  }

  if (status.battery_percent)
    std::cout << "Battery: " << *status.battery_percent << "%\n";
}
```

### Monitoring

`Ups::monitor()` is a **blocking** poll loop on the caller's thread, not an internal worker. After opening `ups`:

```cpp
#include <atomic>
#include <chrono>
#include <iostream>

std::atomic<bool> stop{false};
cyberpower::MonitorOptions options;
options.interval = std::chrono::seconds(2);
options.only_on_change = true;

ups->monitor(options, [](const cyberpower::Status& s) {
  if (s.battery_percent)
    std::cout << "Battery: " << *s.battery_percent << "%\n";
}, stop);
```

Set `stop` to true from your thread or signal-management strategy to exit. `cyberpower::status_changed(prev, next)` compares engineering fields with small numeric tolerance.

### Pure C API and headers

Include `<cyberpower/ups.h>` for the C interface (status, controls, and monitoring). `cp_ups_monitor_with_stop_token()` supports thread-safe cancellation; the `sig_atomic_t` variant is intended for signal-handler cancellation. Ordinary `volatile int` is not thread synchronization.

| Header | Purpose |
| --- | --- |
| `include/cyberpower/ups.hpp` | Public C++ API |
| `include/cyberpower/ups.h` | Public C API |
| `include/cyberpower/protocol.hpp` | Serial, v3, and HID protocol umbrella |
| `include/cyberpower/errors.hpp` | Error codes and messages |

See [Development](development.md) for testing and [Protocol notes](../PROTOCOL.md) for deeper detail.
