# Platform support and hardware validation

This guide distinguishes **implemented transports**, **CI-tested builds**, and **physically verified hardware**. See the [README](../README.md) for downloads and building.

## Platform matrix

| Platform | USB HID | Serial | Verification |
| --- | --- | --- | --- |
| **macOS** | Native IOKit | POSIX `termios` | CP1500PFCLCDa status on Apple Silicon; macOS offline CI |
| **Linux** | Native `hidraw` | POSIX `termios` | CP1500PFCLCDa status on Ubuntu 24.04 ARM64/UTM; Linux CI and ASan/UBSan |
| **Windows** | Not implemented | Not implemented | MSVC build and portable offline CI only; transport stub |

Compiling successfully is **not** evidence of physical UPS support. CyberPower devices sharing Vendor ID `0x0764` may have different report IDs, layouts, and commands.

## Verified HID hardware

**CyberPower CP1500PFCLCDa** (USB VID:PID `0764:0601`) was physically tested:

- **macOS:** Native Apple Silicon HID status reading.
- **Linux:** Ubuntu 24.04 LTS ARM64 in UTM with USB passthrough via `/dev/hidraw*`.
- **Verified:** Discovery, reading eight CP1500PFCLCDa-specific HID Feature reports, and core status parity between macOS/Linux.
- **Observed example values:** Battery 100%, input/output 121 V, 9% load, runtime 4,375 seconds, battery voltage 27.6 V, cycle count 10, AC present, not charging/discharging. These are observations, not expected fixed values.

Optional status fields may be unavailable. Unknown Linux model-specific HID decoding returns `Error::NotSupported` instead of guessing.

### Not physically validated on Linux

- HID alarm/buzzer control writes.
- Self-test, cancel/deep test, and calibration controls.
- Serial communication with an actual CyberPower serial UPS.

Linux vendor configuration writes for sensitivity, shutdown delay, and restore delay intentionally return `Error::NotSupported` until verified. The serial transport has offline pseudo-terminal tests.

**Safety:** Do not experiment with unverified control writes, self-tests, or shutdown commands while the UPS supplies critical equipment. See the [CLI command matrix](usage.md#commands-that-can-change-ups-behavior).

## Linux device permissions (udev)

The provided rule at `udev/99-cyberpower-ups.rules` tags CyberPower HID devices with `TAG+="uaccess"` so active local desktop sessions can obtain access. Actual node modes and permissions vary by distribution.

To install system-wide:

```bash
sudo cp udev/99-cyberpower-ups.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules
# Reconnect the USB device when safe to do so.
```

The build also installs the rule beneath the configured prefix; a user-local prefix may not be in `udev`'s search path. Headless systems may require separate administrator-managed access rules.

## Serial discovery and settings

Generic serial ports are **not probed by default**, avoiding automatic `D\r` requests to unrelated devices:

- Linux: `/dev/ttyS*`, `/dev/ttyUSB*`, `/dev/ttyACM*`.
- macOS: `cu.usbserial*`, `cu.usbmodem*`, `cu.SLAB*`.

Driver-specific names such as `wchusbserial` may be discovered without opt-in.

For explicitly authorized generic serial probing:

```bash
CPUPS_INCLUDE_GENERIC_SERIAL=1 ./build/cpups
```

This can transmit status requests to additional devices, so enable it only when you understand the connected hardware.

Default POSIX settings: raw `termios`, **2400 baud**, **8N1**, `CLOCAL | CREAD`, and nonblocking polling (`VMIN = 0`, `VTIME = 0`). Different devices may need different settings.

## CI validation

GitHub Actions covers Ubuntu and macOS builds, Linux ASan/UBSan, Windows MSVC compilation and portable tests, installed-package CMake/pkg-config consumers, CodeQL, and workflow/format quality checks. GitHub-hosted CI does **not** interact with a physical UPS.

For tests and fixtures see [Development](development.md), [Fixtures](../fixtures/README.md), and [Protocol](../PROTOCOL.md).
