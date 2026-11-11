# Security Policy

## Supported Versions

`cyberpower-ups` is a pre-1.0 C++17 library and CLI tool. Security updates and maintenance are prioritized on the active development branch and recent releases.

| Version | Status | Security Support Level |
| :--- | :--- | :--- |
| `main` (0.7.x development) | Supported | Actively maintained; receives prompt security and correctness fixes |
| Latest tagged release (`v0.7.0`) | Supported | Best-effort security patches and bug fixes |
| Prior release versions | Unsupported | Older versions do not receive backported security patches; users should upgrade |

## Reporting a Vulnerability

We take the security and integrity of `cyberpower-ups` seriously. If you discover a security vulnerability, **please do not report it in public GitHub issues, discussions, or pull requests.**

### Preferred Reporting Method

Please report vulnerabilities privately via **[GitHub Private Vulnerability Reporting](https://github.com/andrewtryder/cyberpower-ups/security/advisories/new)**.

> [!NOTE]
> If private vulnerability reporting is not yet visible in the repository, the repository maintainer can enable it under:
> **Repository Settings → Code security and analysis → Private vulnerability reporting → Enable**.

When submitting a vulnerability report, please include:
- A description of the vulnerability and its potential impact.
- Affected versions, platforms (macOS / Linux), and transports (USB HID / serial).
- Minimal reproduction steps, proof of concept, or sanitized crash log.
- Any known mitigations or suggested fixes.

## Security Response Process

For all acknowledged reports, the maintainer will:
1. **Acknowledge and Assess**: Privately acknowledge receipt and begin assessment within a reasonable timeframe.
2. **Reproduce & Investigate**: Verify the issue against offline mock environments or hardware where available.
3. **Develop & Test Fix**: Create and test a fix in private to ensure no regressions or hardware side effects.
4. **Coordinate Disclosure**: Agree on a coordinated disclosure timeline with the reporter.
5. **Publish Advisory & Release**: Publish a GitHub Security Advisory and release a patched version.

Because this project is maintained by a solo developer, response and resolution times are on a best-effort basis without contractual service level agreements.

## Hardware Safety & Operational Considerations

Unlike pure software libraries, `cyberpower-ups` communicates directly with physical Uninterruptible Power Supply (UPS) hardware that supplies electrical power to host computers and peripherals.

Contributors and users should be aware of important hardware safety boundaries:

- **Control Writes & Tests**: Commands such as self-test initiation, buzzer silencing, calibration, or scheduled tests execute firmware routines on physical hardware. Unsupported or unverified commands may put hardware in unexpected operational modes.
- **Power Shutdown Operations**: Commands that cut load power or trigger battery-cutoff sequences (such as serial `S` shutdown commands) will physically disconnect downstream electrical loads.
- **Untrusted Input Parsing**: The library parses binary HID feature/input reports and serial ASCII protocol responses. Parsers employ bounds checks, Feature report length validation, report-ID verification, and defensive integer conversions to validate incoming data before interpretation. While these mechanisms guard against obvious parsing defects, comprehensive memory safety and resilience to arbitrary malformed inputs require ongoing sanitizer and fuzzing coverage.
- **Device Verification**: Linux HID enumeration identifies candidate devices using CyberPower's vendor ID (`0x0764`), and specific device-profile checks occur when decoding model-specific status reports or performing restricted control operations. Enumeration does not reject unsupported CyberPower PIDs upfront. Serial discovery avoids probing unverified serial ports unless explicitly opted into via `CPUPS_INCLUDE_GENERIC_SERIAL=1`, preventing unintended protocol transmissions to unrelated hardware.
- **Software Vulnerability vs. Unsupported Hardware**: Software vulnerabilities (buffer overflows, parser crashes, unexpected privileges) are treated as security issues. Hardware behavior differences due to unknown vendor firmware revisions or unsupported models are treated as compatibility issues rather than security defects.

