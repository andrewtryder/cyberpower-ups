<p align="center">
  <img src="docs/images/cyberpower-logo.png" alt="CyberPower UPS" width="600">
</p>

# cyberpower-ups

A modern C++17 library and command-line tool for talking to CyberPower UPS devices over **USB HID** and **serial**.

## Why this project exists

Official **PowerPanel Personal** ships a native driver, `libppbedrvc.dylib`, that is **x86_64-only** — not a native Apple Silicon / arm64 binary. On Apple Silicon Macs that forces Rosetta (or leaves you without a clean native stack) for direct USB HID and serial access to the UPS.

**cyberpower-ups** was built by reverse-engineering that driver so there is a **native C++17 library and CLI** that talks to the hardware directly: no dependency on the proprietary dylib, and no Rosetta required for this code path. It works today on **macOS** (IOKit HID + termios) and **Linux** (`/dev/hidraw*` + termios); Windows builds compile against a transport stub while the platform layer stays isolated.

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
sudo cmake --install build
```

This installs:

- `libcyberpower-ups` (static library)
- public headers under `include/cyberpower/`
- the `cpups` binary
- CMake package config under `lib/cmake/cyberpower-ups/`
- pkg-config file `lib/pkgconfig/cyberpower-ups.pc`
- on Linux: udev rule under `lib/udev/rules.d/99-cyberpower-ups.rules` (grants access to `/dev/hidraw*` devices for local active console users)

---

## Linux Support & Hardware Validation

Linux support uses the native kernel `hidraw` interface for USB HID devices and POSIX `termios` for serial ports.

### Hardware Validation
- **Environment**: Ubuntu 24.04 LTS (kernel 6.8, GCC 13.3.0).
- **Verified Hardware**: CyberPower CP1500PFCLCDa (USB VID `0x0764`, PID `0x0601`).
- **Verified HID Status Readings**:
  - Battery capacity (%)
  - Input & output AC voltage (V)
  - Output load (%)
  - Remaining runtime (seconds)
  - AC line presence, charging, and discharging flags
  - Battery pack voltage (V) and cycle count
  - Voltage sensitivity level (Medium / Normal)

### Verified vs. Experimental Controls
- **Verified Write Support**:
  - Audible alarm control (Mute, Enable, Disable) via Feature Report `0x0C` (VID `0x0764`, PID `0x0601`).
  - Battery quick self-test and test abort via Feature Report `0x14` (VID `0x0764`, PID `0x0601`).
  - All input parameters are strictly validated (`1..3`); out-of-range values return `Error::ReqParamOutOfRange`.
- **Safety Policy for Unverified Models**:
  - Unverified HID models or unsupported product IDs return `Error::NotSupported` rather than guessing report offsets.
  - Speculative vendor configuration writes (`set_voltage_sensitivity`, `set_shutdown_delay`, `set_restore_delay`) are rejected on Linux `hidraw` with `Error::NotSupported` pending full runtime descriptor parsing.
  - > [!WARNING]
    > **Do not execute unverified control operations against production UPS equipment.** Testing or configuration writes should only be run against verified devices in non-critical environments.

### Linux `hidraw` Device Permissions (udev)
By default, `/dev/hidraw*` device nodes are owned by `root:root` with mode `0600`. To allow non-root users (or the active desktop session) to read UPS status, install the provided udev rule:

```bash
sudo cp udev/99-cyberpower-ups.rules /etc/udev/rules.d/
sudo udevadm control --reload
sudo udevadm trigger
```

The rule tags CyberPower USB devices (`ATTRS{idVendor}=="0764"`) with `TAG+="uaccess"`, giving the currently logged-in desktop user read/write access to the device node.

### Serial Discovery Safety & Opt-in
On Linux, generic serial ports (`/dev/ttyS*`, `/dev/ttyUSB*`, `/dev/ttyACM*`) are **not** probed by default during automatic device discovery. This prevents transmitting `D\r` or serial control strings to unrelated peripherals, modems, or motherboards.

To explicitly probe generic serial ports, set `CPUPS_INCLUDE_GENERIC_SERIAL=1`:
```bash
CPUPS_INCLUDE_GENERIC_SERIAL=1 cpups
```
Driver-specific names recognized by the original driver (such as `wchusbserial`) are always discovered safely.

### Linux POSIX Termios Serial Configuration
Serial communication on Linux is configured using standard POSIX `termios` flags:
- Raw mode (`cfmakeraw`)
- 2400 baud default
- 8 data bits, no parity, 1 stop bit (8N1)
- `CLOCAL | CREAD` (local line, receiver enabled)
- Nonblocking read polling (`VMIN = 0`, `VTIME = 0`)

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

`cyberpower::status_changed(prev, next)` compares engineering fields (with a small numeric epsilon). The C API has two monitor variants: `cp_ups_monitor(...)` accepts a `volatile sig_atomic_t*` only for a signal handler; `cp_ups_monitor_with_stop_token(...)` uses a thread-safe stop token for another thread. An ordinary `volatile int` is not thread synchronization.

A pure C API is also available in `cyberpower/ups.h` (`cp_ups_self_test`, `cp_ups_toggle_buzzer`, `cp_ups_monitor`, …).

### Serial discovery safety

By default serial discovery only includes port-name patterns explicitly named by the recovered driver. Generic macOS USB-serial names are excluded so the default command does not send `D\r` to arbitrary peripherals. Set `CPUPS_INCLUDE_GENERIC_SERIAL=1` only when you intentionally want to probe those generic ports; all other values leave them excluded.

### Headers

| Header | Purpose |
|--------|---------|
| `include/cyberpower/ups.hpp` | Main C++ API (`list_devices`, `open_device`, `read_status`, …) |
| `include/cyberpower/ups.h` | Same operations as a C API |
| `include/cyberpower/protocol.hpp` | Umbrella for `protocol_text.hpp` (commands, v2e parser), `protocol_v3.hpp` (binary framing) and `protocol_hid.hpp` (HID usages) |
| `include/cyberpower/errors.hpp` | `UPS_ERR_*` and `RESP_ERR_*` |

---

## Project Layout

```
cyberpower-ups/
├── include/cyberpower/   # Public headers
├── src/
│   ├── platform/macos/   # IOKit (hid_macos) + termios (serial_macos)
│   ├── platform/linux/   # hidraw (hid_linux) + termios (serial_linux)
│   └── platform/windows/ # Stub / extension points for SetupAPI + COM
├── tools/                # cpups CLI (cpups.cpp, cpups_args, cpups_format)
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

## Code Quality & Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for full development guidelines, issue templates, PR title conventions, and CI quality checks.

Helpful Make targets:
```bash
make test          # Run ctest and offline self-tests
make check-format  # Check incremental C/C++ formatting with git-clang-format
make format        # Format modified lines in working tree
make lint          # Run actionlint and zizmor workflow scanner
```

CI automatically enforces:
- Build and test passes on Ubuntu and macOS
- Conventional Commits PR titles (`feat:`, `fix:`, `ci:`, etc.)
- Incremental formatting on C/C++ changes (`clang-format`)
- Workflow linting (`actionlint`) and GitHub Actions security scanning (`zizmor`)

---

## Platform & Hardware Compatibility

### Operating System Support

| Platform | Support Status | Implementation Details |
| :--- | :--- | :--- |
| **macOS** | Supported (arm64 & x86_64) | Native IOKit HID (`IOHIDManager`) and POSIX `termios` serial |
| **Linux** | Supported | Native `hidraw` (`/dev/hidraw*`) and POSIX `termios` serial; continuous CI coverage includes AddressSanitizer and UndefinedBehaviorSanitizer |
| **Windows** | Stub only (CI verified) | Compiles cleanly with MSVC on `windows-latest` CI; offline parser self-tests and test fixtures execute portable logic |

### Hardware Validation & Model Support

The reverse-engineered protocol originates from PowerPanel Personal driver analysis (`libppbedrvc.dylib`). However, hardware capabilities, Feature report IDs, and report layouts vary significantly across CyberPower product families:

- **Physically Validated Profile (Status Reading)**: The **CyberPower CP1500PFCLCDa** (USB VID `0x0764`, PID `0x0601`) has been physically validated on:
  - **macOS 14+** (Apple Silicon native USB HID)
  - **Ubuntu 24.04** (ARM64 under UTM VM via USB passthrough to `/dev/hidraw*`)
  Physically confirmed functionality includes:
  - Native Linux `hidraw` device discovery
  - Reading the eight CP1500PFCLCDa-specific Feature reports
  - Decoding core status fields: battery capacity (100%), line input/output voltages (121 V), load percentage (9%), remaining runtime (4375 s), AC presence, and charging/discharging states
  - Full parity of core status readings between Linux and macOS
- **Implemented but Not Physically Validated**:
  - Linux HID alarm control writes (buzzer toggle, mute)
  - Battery self-test initiation and cancellation commands
  - Battery calibration and deep test operations
  - Linux serial communication against an actual CyberPower serial UPS (serial transport is tested via offline pseudo-terminal emulation)
- **Model Independence**: Support for other CyberPower UPS models **must not be inferred** merely from a matching vendor ID (`0x0764`). Other models may use different report IDs, alternative usage paths, or distinct serial baud/framing parameters.
- **Defensive Operation**: Unsupported or unverified control operations (such as feature writes on unconfirmed PIDs or unverified report IDs) intentionally return `Error::NotSupported` or `Error::ReqParamOutOfRange` rather than issuing speculative control writes.

---

## Security & Governance

- **Security Policy**: See [SECURITY.md](SECURITY.md) for supported versions, vulnerability reporting instructions via GitHub Private Vulnerability Reporting, and hardware safety considerations.
- **Governance & Branch Protection**: See [docs/governance.md](docs/governance.md) for project maintenance policies, Dependabot automation, and recommended branch protection rules.
- **Semantic Versioning & Releases**: See [docs/versioning.md](docs/versioning.md) for our pre-1.0 SemVer policy, API/ABI compatibility boundaries, and automated Release Please workflows.

---

## License & Disclaimer

MIT License — see [LICENSE](LICENSE).

This is an independent reverse-engineered library. It is **not** affiliated with, endorsed by, or supported by Cyber Power Systems, Inc. Use at your own risk. The authors are not responsible for any damage to hardware or data.

---

## Credits

Protocol recovered from static analysis of PowerPanel Personal’s `libppbedrvc.dylib` (x86_64).
