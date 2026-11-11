# Changelog

## [0.7.0](https://github.com/andrewtryder/cyberpower-ups/releases/tag/v0.7.0) (2026-09-28)

### Highlights
- Safer macOS serial discovery: generic USB-serial devices are no longer probed by default, preventing `cpups` from sending status commands to unrelated hardware. Set `CPUPS_INCLUDE_GENERIC_SERIAL=1` only to explicitly include generic ports.
- Safer C monitoring semantics: ordinary `volatile int` is no longer presented as thread synchronization. Use `cp_ups_monitor_with_stop_token()` for thread-safe stopping, or the signal-only `sig_atomic_t` monitor variant when stopping from a signal handler.
- Improved robustness across v2e parsing, HID compatibility mapping, nonblocking serial writes, hardware-setting validation, buzzer error propagation, and installed-package linking.

## [0.6.0](https://github.com/andrewtryder/cyberpower-ups/releases/tag/v0.6.0) (2026-09-28)

### Highlights
- HID Mapping Fixes: Fixed a regression in HID macOS mapping. Feature elements are now correctly allowed for status reads, and legacy usages for battery and runtime are prioritized over newer dummy usages on CP1500PFCLCDa.
- Extended Fields & Config Writes: Exposes configuration writes on vendor page 0xFF86, including voltage sensitivity, shutdown delay, and restore delay.
- Firmware String: Removed the fallback that blindly copied the USB iProduct string into the firmware version field.

## [0.5.0](https://github.com/andrewtryder/cyberpower-ups/releases/tag/v0.5.0) (2026-09-28)

### Highlights
- Native HID controls, Makefile convenience targets, and clearer docs for Apple Silicon.
- HID alarm / self-test for models like CP1500PFCLCDa (PID `0x0601`): Feature reports alarm `0x0C` values 1/2/3 and test `0x14` values 1/2/3 via `IOHIDDeviceSetValue` (SetReport fallback). `cpups --buzzer`, `--test`, `--cancel-test`, `--mute` / `--enable-alarm` / `--disable-alarm` work on HID; serial text path unchanged.
- Makefile wrapper around CMake (`make`, `make test`, `make run`, `make monitor`, `make install`, `make clean` / `distclean`).
- Docs: why the project exists (native arm64-capable C++17 stack with no proprietary dylib).
