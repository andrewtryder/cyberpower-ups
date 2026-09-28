# cyberpower-ups

A modern C++17 library and command-line tool for talking to CyberPower UPS devices over **USB HID** and **serial**.

The wire protocol was recovered by static analysis of PowerPanel Personal’s native driver (`libppbedrvc.dylib`). This library does **not** link or depend on that dylib — it speaks to the hardware directly.

- Works today on **macOS** (IOKit HID + termios)
- Windows / Linux builds compile against a transport stub (platform layer is isolated)
- Clean public C++ and C APIs so you can use it from other applications
- Direct device access only (no network / TCP/IP card required)

For protocol internals, recovered constants, and confidence annotations, see **[PROTOCOL.md](PROTOCOL.md)**.

---

## Features

- Device discovery (USB HID + serial ports)
- Status polling (voltage, load, battery %, runtime, AC present, and more)
- High-level serial commands (self-test, cancel test, toggle buzzer, rating, …)
- Polling monitor API (`Ups::monitor`) and `cpups --monitor`
- JSON output via the `cpups` CLI
- Recovered text protocol (v1 / v2e / titan) and v3 binary framing
- HID Power Device / Battery usage support

---

## Quick Start

```bash
cmake -S . -B build
cmake --build build

# List devices and show status
./build/cpups

# Run parser + CRC self-test (no hardware needed)
./build/cpups --self-test

# JSON output
./build/cpups --json

# Live monitor (first device; Ctrl-C to stop)
./build/cpups --monitor
./build/cpups --monitor --interval 1000
./build/cpups --monitor --json          # JSON lines on change
./build/cpups --monitor --every-poll    # print every sample

# Device commands (first UPS; serial; HID returns NotSupported)
./build/cpups --test                    # or --self-test-device
./build/cpups --cancel-test
./build/cpups --buzzer                  # or --beep
./build/cpups --rating
./build/cpups --cancel-schedule
./build/cpups --calibrate
./build/cpups --indicator-test
./build/cpups --buzzer-test
./build/cpups --rating --json

# Capture raw HID reports / serial lines to stderr
./build/cpups --dump-raw
./build/cpups --json --dump-raw 2> fixtures/raw.txt
```

`cpups` is the day-to-day CLI. `examples/list_and_status` is a thinner library sample if you want a starting point for your own code.

### Install

```bash
cmake -S . -B build -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build
cmake --install build
```

This installs:

- `libcyberpower-ups` (static library)
- public headers under `include/cyberpower/`
- the `cpups` binary
- CMake package config under `lib/cmake/cyberpower-ups/`
- pkg-config file `lib/pkgconfig/cyberpower-ups.pc`

---

## Using the Library

### From CMake (`find_package`)

```cmake
find_package(cyberpower-ups REQUIRED)
target_link_libraries(myapp PRIVATE cyberpower-ups::cyberpower-ups)
```

### From pkg-config

```bash
pkg-config --cflags --libs cyberpower-ups
```

### C++ example

```cpp
#include <cyberpower/ups.hpp>
#include <iostream>

int main() {
  auto devices = cyberpower::list_devices();
  if (devices.empty()) {
    std::cerr << "No UPS found\n";
    return 1;
  }

  auto ups = cyberpower::open_device(devices[0]);
  if (!ups) {
    std::cerr << "Failed to open device\n";
    return 1;
  }

  auto status = ups->read_status();
  if (status.battery_percent) {
    std::cout << "Battery: " << *status.battery_percent << "%\n";
  }

  // Serial-only helpers (HID returns NotSupported):
  // ups->self_test();
  // ups->cancel_test();
  // ups->toggle_buzzer();
  // std::string rating; ups->read_rating(rating);
}
```

### Monitor API

`Ups::monitor()` is a **blocking** poll loop on the calling thread (no background worker). Set `stop_flag` to leave the loop — sleep is interruptible in ~50 ms slices.

```cpp
#include <atomic>
#include <chrono>
#include <iostream>

std::atomic<bool> stop{false};
cyberpower::MonitorOptions opts;
opts.interval = std::chrono::seconds(2);
opts.only_on_change = true;  // first sample + meaningful changes

ups->monitor(opts, [](const cyberpower::Status& s) {
  if (s.battery_percent) {
    std::cout << "battery " << *s.battery_percent << "%\n";
  }
}, stop);
```

`cyberpower::status_changed(prev, next)` compares engineering fields (with a small numeric epsilon). The C API mirror is `cp_ups_monitor(...)` with a `volatile int* stop_flag`.

A pure C API is also available in `cyberpower/ups.h` (`cp_ups_self_test`, `cp_ups_toggle_buzzer`, `cp_ups_monitor`, …).

### Headers

| Header | Purpose |
|--------|---------|
| `include/cyberpower/ups.hpp` | Main C++ API (`list_devices`, `open_device`, `read_status`, …) |
| `include/cyberpower/ups.h` | Same operations as a C API |
| `include/cyberpower/protocol.hpp` | Commands, parsers, HID usages |
| `include/cyberpower/errors.hpp` | `UPS_ERR_*` and `RESP_ERR_*` |

---

## Project Layout

```
cyberpower-ups/
├── include/cyberpower/   # Public headers
├── src/
│   ├── platform/macos/   # IOKit + termios
│   └── platform/windows/ # Stub / extension points for SetupAPI + COM
├── tools/cpups.cpp       # Command-line tool
├── tests/                # Offline CTest (no UPS required)
├── fixtures/             # Capture instructions + optional dumps
├── examples/             # Library samples
├── cmake/                # Package config + pkg-config templates
├── PROTOCOL.md           # Protocol notes & reverse-engineering detail
└── CMakeLists.txt
```

---

## Testing / Fixtures

Offline tests need no UPS. They run the protocol self-test, parse hard-coded v2e status frames, and load JSON fixtures when present:

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
# or: ./build/cpups --self-test
```

Capture real-device fixtures (optional; see [fixtures/README.md](fixtures/README.md)):

```bash
./build/cpups --json > fixtures/cp1500pfclcda_status.json
./build/cpups --rating > fixtures/cp1500pfclcda_rating.txt
./build/cpups --dump-raw 2> fixtures/cp1500pfclcda_raw.txt
```

`--dump-raw` writes serial TX/RX lines or HID input-report hex to **stderr** so stdout can stay JSON-clean.

---

## Supported Models

The original driver recognizes many CPS models (EI, PIE, PRO, OR, PR, OL, PP, and more). This library speaks the common **v2e `D` text protocol** on serial and **standard HID usages** on USB. Model-specific branches from the original driver are not all specialized here yet.

---

## License & Disclaimer

MIT License — see [LICENSE](LICENSE).

This is an independent reverse-engineered library. It is **not** affiliated with, endorsed by, or supported by Cyber Power Systems, Inc. Use at your own risk. The authors are not responsible for any damage to hardware or data.

---

## Credits

Protocol recovered from static analysis of PowerPanel Personal’s `libppbedrvc.dylib`.
