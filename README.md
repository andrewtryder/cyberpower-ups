# cyberpower-ups

**cyberpower-ups** is a native **C++17 library, pure C API, and CLI** for monitoring CyberPower uninterruptible power supplies (UPS) over **USB HID** and **serial**. It reports battery level, runtime, voltages, output load, and power state. Selected hardware controls are also implemented where supported.

The project provides direct UPS access on Apple Silicon macOS without PowerPanel Personal's x86_64-only driver or Rosetta, plus a native Linux transport. This independent project is not affiliated with Cyber Power Systems.

## Get the source

Download a source archive from [GitHub Releases](https://github.com/andrewtryder/cyberpower-ups/releases), or clone the development branch:

```bash
git clone https://github.com/andrewtryder/cyberpower-ups.git
cd cyberpower-ups
```

Releases are **source-only**; precompiled binaries are not currently distributed.

## Build and run

Requires **CMake 3.16+** and a **C++17 compiler** (AppleClang/Clang, GCC, or MSVC).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

On macOS and Linux, connect a compatible UPS and read its status:

```bash
./build/cpups
./build/cpups --json
./build/cpups --monitor
```

Windows currently uses a **transport stub**: it compiles and passes portable offline tests but does not communicate with physical UPS hardware. Hardware-changing CLI operations require additional care; see the [CLI guide](docs/usage.md).

### Install and integrate

To install to a user-owned prefix:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build --config Release
cmake --install build --config Release
```

The install contains the library, public C/C++ headers, CLI, exported CMake package, and pkg-config metadata. In a downstream CMake target:

```cmake
find_package(cyberpower-ups REQUIRED)
target_link_libraries(myapp PRIVATE cyberpower-ups::cyberpower-ups)
```

Set `CMAKE_PREFIX_PATH` to the install prefix if necessary. See [Using the library](docs/usage.md#use-the-library) for C++ examples, the C API, monitoring, and pkg-config.

## Where it works and what is tested

| Platform | Implementation | Validation |
| --- | --- | --- |
| **macOS** | Native IOKit HID and POSIX serial | CP1500PFCLCDa HID status verified on Apple Silicon; macOS build/offline CI |
| **Linux** | Native `hidraw` HID and POSIX serial | CP1500PFCLCDa HID status verified on Ubuntu 24.04 ARM64 (UTM USB passthrough); Linux CI and ASan/UBSan |
| **Windows** | Stub only | MSVC build and portable offline tests pass; no hardware transport |

**Physically validated USB model:** CyberPower **CP1500PFCLCDa** (VID:PID `0764:0601`). Verified macOS/Linux status readings include battery capacity, input/output voltage, load, runtime, AC state, battery voltage, and cycle count. Its eight recovered HID Feature report IDs are **model-specific**.

**Not physically validated on Linux:** HID alarm/self-test control writes and serial communication with an actual CyberPower UPS. Serial configuration and transactions are tested with offline pseudo-terminals. A shared CyberPower vendor ID does **not** imply support for all models.

For Linux device permissions, safe serial discovery, and further hardware notes see [Platforms and hardware](docs/platforms.md).

## Documentation

- [CLI usage, C++/C APIs, and examples](docs/usage.md)
- [Platform support, verified hardware, and Linux setup](docs/platforms.md)
- [Developer builds, testing, fixtures, and project layout](docs/development.md)
- [Protocol and reverse-engineering notes](PROTOCOL.md)
- [Contributing](CONTRIBUTING.md) · [Security](SECURITY.md)
- [Release policy](docs/versioning.md) · [Changelog](CHANGELOG.md) · [Governance](docs/governance.md)

## License and disclaimer

[MIT License](LICENSE). This is an independent reverse-engineered library, not affiliated with, endorsed by, or supported by Cyber Power Systems, Inc. Use at your own risk; the authors are not responsible for hardware damage or data loss.
