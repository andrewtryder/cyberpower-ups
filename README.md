# cyberpower-ups

A modern C++17 library and command-line tool for talking to CyberPower UPS devices over **USB HID** and **serial**.

## Why this project exists

Official **PowerPanel Personal** ships a native driver, `libppbedrvc.dylib`, that is **x86_64-only** — not a native Apple Silicon / arm64 binary. On Apple Silicon Macs that forces Rosetta (or leaves you without a clean native stack) for direct USB HID and serial access to the UPS.

**cyberpower-ups** was built by reverse-engineering that driver so there is a **native C++17 library and CLI** that talks to the hardware directly: no dependency on the proprietary dylib, and no Rosetta required for this code path. It works today on macOS (IOKit HID + termios); Windows / Linux builds compile against a transport stub while the platform layer stays isolated.

This project is independent and **not affiliated with** Cyber Power Systems. See [License & Disclaimer](#license--disclaimer).

For protocol internals, recovered constants, and confidence annotations, see **[PROTOCOL.md](PROTOCOL.md)**.

---

## Features

- Device discovery (USB HID + serial ports)
- Status polling (voltage, load, battery %, runtime, AC present, and more)
- High-level commands on HID and/or serial (self-test, cancel test, alarm control, calibrate, …)
- Polling monitor API (`Ups::monitor`) and `cpups --monitor`
- JSON output and `--dump-raw` capture via the `cpups` CLI
- Recovered text protocol (v1 / v2e / titan) and v3 binary framing
- HID Power Device / Battery status usages and alarm/test Feature controls (e.g. PID `0x0601`)

---

## Quick Start

CMake is the real build system. A top-level **Makefile** is a thin convenience wrapper.

### With Make

```bash
make                 # configure + Release build in ./build
make test            # ctest + ./build/cpups --self-test (no UPS required)
make run             # ./build/cpups
make run ARGS='--json'
make monitor         # ./build/cpups --monitor
make install         # cmake --install (PREFIX=/usr/local by default)
make help            # list targets and overrides
```

Overridable variables: `BUILD_TYPE` (default `Release`), `PREFIX` (default `/usr/local`), `ARGS` (extra `cpups` flags for `run` / `monitor` / `json`).

```bash
make BUILD_TYPE=Debug
make install PREFIX=/opt/local
make clean           # remove build artifacts; keep ./build
make distclean       # delete ./build
```

### With CMake directly

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

# Device commands (first UPS; see HID vs serial below)
./build/cpups --test                    # quick self-test (HID+serial)
./build/cpups --cancel-test
./build/cpups --buzzer                  # toggle alarm (HID+serial)
./build/cpups --mute                    # HID Feature mute
./build/cpups --calibrate               # serial TL, or HID deep Test(2)
./build/cpups --rating                  # serial only
./build/cpups --cancel-schedule         # serial only
./build/cpups --indicator-test          # serial only
./build/cpups --buzzer-test             # serial only
./build/cpups --rating --json

# Capture raw HID reports / serial lines to stderr
./build/cpups --dump-raw
./build/cpups --json --dump-raw 2> fixtures/raw.txt
```

`cpups` is the day-to-day CLI. `examples/list_and_status` is a thinner library sample if you want a starting point for your own code.

### Install

```bash
make install
# or:
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

## HID vs serial controls

Status reads work on both transports. Control commands differ:

| Command | HID | Serial |
| --- | --- | --- |
| `--test` / `self_test()` | Test(1) Feature write | `T\r` |
| `--cancel-test` / `cancel_test()` | Test(3) | `CT\r` |
| `--buzzer` / `toggle_buzzer()` | mute if enabled, else enable | `B\r` |
| `--mute` / `--enable-alarm` / `--disable-alarm` | Feature alarm values 3 / 2 / 1 | NotSupported |
| `--calibrate` / `calibrate()` | Test(2) deep (closest HID) | `TL\r` |
| `--rating`, `--cancel-schedule`, `--indicator-test`, `--buzzer-test` | NotSupported | `F\r` / `C\r` / `TI\r` / `TB\r` |

On **CP1500PFCLCDa** (VID `0x0764`, PID `0x0601`), recovered Feature reports:

| Action | Report ID | Value |
| --- | --- | --- |
| Disable / Enable / Mute alarm | `0x0C` | 1 / 2 / 3 |
| Quick / Deep / Abort test | `0x14` | 1 / 2 / 3 |

Full reverse-engineering detail and confidence tags: **[PROTOCOL.md](PROTOCOL.md)**.

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

  // HID+serial where supported:
  // ups->self_test();
  // ups->cancel_test();
  // ups->toggle_buzzer();
  // ups->mute_alarm();
  // std::string rating; ups->read_rating(rating);  // serial only
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
├── Makefile              # Thin wrapper around CMake
├── PROTOCOL.md           # Protocol notes & reverse-engineering detail
└── CMakeLists.txt
```

---

## Testing / Fixtures

Offline tests need no UPS. They run the protocol self-test, parse hard-coded v2e status frames, and load JSON fixtures when present:

```bash
make test
# or:
cmake -S . -B build && cmake --build build
ctest --test-dir build --output-on-failure
./build/cpups --self-test
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

The original driver recognizes many CPS models (EI, PIE, PRO, OR, PR, OL, PP, and more). This library speaks the common **v2e `D` text protocol** on serial and **standard HID usages** on USB. Model-specific branches from the original driver are not all specialized here yet. HID alarm/test Feature writes are confirmed for PID `0x0601` (e.g. CP1500PFCLCDa); other product IDs resolve report IDs from the device descriptor when present.

---

## License & Disclaimer

MIT License — see [LICENSE](LICENSE).

This is an independent reverse-engineered library. It is **not** affiliated with, endorsed by, or supported by Cyber Power Systems, Inc. Use at your own risk. The authors are not responsible for any damage to hardware or data.

---

## Credits

Protocol recovered from static analysis of PowerPanel Personal’s `libppbedrvc.dylib` (x86_64).
