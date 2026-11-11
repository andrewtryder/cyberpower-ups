# Semantic Versioning & Release Policy

This document defines the versioning strategy, public API compatibility commitments, and release automation for `cyberpower-ups`.

---

## 1. Semantic Versioning Specification

`cyberpower-ups` adheres to [Semantic Versioning 2.0.0](https://semver.org/) (`MAJOR.MINOR.PATCH`).

The current release baseline is in the **`0.x` development series** (baseline: `0.7.0`).

### 1.1 Pre-1.0 Policy (`0.x`)

During the `0.x` cycle, rapid development, reverse-engineering discoveries, and transport hardening occur. Version increments follow an explicit pre-1.0 specification:

| Change Type | Conventional Commit | Version Increment | Example from `0.7.0` |
| :--- | :--- | :--- | :--- |
| **Bug Fix** | `fix:` or `fix(scope):` | **Patch** release | `0.7.1` |
| **New Feature** | `feat:` or `feat(scope):` | **Minor** release | `0.8.0` |
| **Breaking Change (pre-1.0)** | `feat!:` or `BREAKING CHANGE:` | **Minor** release (prominently documented) | `0.8.0` |
| **Maintenance / Non-functional** | `docs:`, `test:`, `ci:`, `chore:`, `refactor:`, `style:` | **No automated release** | None |

#### Key Rules for Pre-1.0:
- **Patch releases (`0.7.x`)**: Reserved strictly for backward-compatible bug fixes, transport hardening, and diagnostic improvements.
- **Minor releases (`0.x.0`)**: Introduce new public API features or pre-1.0 breaking API modifications.
- **Version 1.0.0**: **Will never be created automatically.** Reaching `1.0.0` represents an explicit, deliberate decision by the repository maintainer once public APIs and hardware abstractions are deemed stable and comprehensive.

---

## 2. Public Compatibility Commitments

The project consists of several distinct layers. Changes to each layer are evaluated independently for compatibility:

### 2.1 C++ Public API (`include/cyberpower/*.hpp`)
- **Compatibility Boundary**: The public C++ interface includes headers under `include/cyberpower/` (such as `ups.hpp`, `protocol.hpp`, `protocol_text.hpp`, `protocol_v3.hpp`, `protocol_hid.hpp`, and `errors.hpp`). Internal headers under `src/` or `src/internal/` are private and exempt from stability guarantees.
- **Breaking Changes**: Modifying function signatures, removing public types or methods, changing return types, or altering error enum semantics.

### 2.2 C API and Binary ABI (`include/cyberpower/ups.h`)
- **Compatibility Boundary**: The pure C interface declared in `include/cyberpower/ups.h` provides a stable C ABI suitable for foreign-function interfaces (FFI) and dynamic linking across language boundaries.
- **ABI Guarantees**: Symbol names (`cp_ups_*`), struct layouts (`cp_ups_device_info`, `cp_ups_status`), and calling conventions are preserved across patch and minor releases.
- **C++ ABI Note**: C++ libraries do not maintain binary compatibility across different compilers, compiler versions, or standard library implementations (`libstdc++` vs. `libc++`). Downstream C++ consumers must compile with matching C++17 flags.

### 2.3 Command-Line Interface (`cpups`)
- **Compatibility Boundary**: The CLI command flags (`--json`, `--monitor`, `--test`, `--buzzer`, `--self-test`), exit codes (`0` for success, non-zero for errors), and JSON schema (`cpups --json`).
- **Breaking Changes**: Removing flags, changing existing JSON field names or types, or altering exit code meanings. Adding new optional flags or additional JSON keys is backward-compatible.

### 2.4 Transport & Protocol Parsers
- **Compatibility Boundary**: Status frame parsing (`#` ... `\r`), v3 CRC-8 framing, and HID usage mappings.
- **Hardening vs. Breaking**: Rejecting malformed frames, handling out-of-bounds report values, or enforcing safety bounds does **not** constitute a breaking change; it is categorized as a bug fix (`fix:`).

### 2.5 UPS Hardware Compatibility
> [!IMPORTANT]
> **A new library version does NOT imply compatibility with new UPS models.**
>
> CyberPower devices share a common USB Vendor ID (`0x0764`), but report IDs, feature sizes, and protocol command sets differ drastically across model lines.
> Compatibility with any model (such as the verified `CP1500PFCLCDa` `0764:0601`) requires physical or descriptor-level validation. Unsupported models safely return `Error::NotSupported`. Adding verified support for a new model is a feature (`feat:`).

---

## 3. CMake Library Versioning & `SOVERSION`

In [`CMakeLists.txt`](../CMakeLists.txt), the library target properties are configured as:

```cmake
set_target_properties(cyberpower-ups PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION 0
  EXPORT_NAME cyberpower-ups
  OUTPUT_NAME cyberpower-ups
)
```

- **`VERSION`**: Matches the project release version (e.g., `0.7.0`, `0.7.1`). On ELF systems, this produces the fully qualified shared library filename (e.g., `libcyberpower-ups.so.0.7.0`).
- **`SOVERSION`**: Identifies the ABI soname version (currently `0`).
- **Policy**: `SOVERSION` is **not** bumped on patch or minor releases. It will only be incremented when an intentional, breaking change to the exported binary ABI is introduced by the maintainer.

---

## 4. Release Please Automation

Releases and changelogs are managed automatically using **[Release Please](https://github.com/googleapis/release-please)**.

### 4.1 How It Works
1. **Conventional Commits**: Every PR merged into `main` has a squash title following the Conventional Commits specification.
2. **Release PR**: Release Please analyzes commits on `main` since the last release tag (`v0.7.0` bootstrap). When release-worthy commits (`fix:` or `feat:`) are detected, Release Please opens or updates a dedicated release pull request (e.g. `chore(main): release 0.7.1`).
3. **Synchronized Files**: The release PR automatically updates:
   - Declared `VERSION` in `CMakeLists.txt`
   - `version.txt`
   - `.release-please-manifest.json`
   - `CHANGELOG.md`
4. **Maintainer Review & Merge**:
   - The maintainer reviews the release PR and verifies all 6 required CI status checks pass.
   - The maintainer squash-merges the release PR into `main`.
5. **Tag & Release Publication**:
   - Once merged, Release Please creates the Git tag (`vMAJOR.MINOR.PATCH`, e.g., `v0.7.1`) and publishes a GitHub Release with compiled release notes.

### 4.2 Version Synchronization Guard
A deterministic verification script ([`scripts/check-version-sync.py`](../scripts/check-version-sync.py)) executes in CTest across all platforms (Ubuntu, macOS, Windows) and in CI. It ensures that `CMakeLists.txt`, `version.txt`, and `.release-please-manifest.json` remain identical at all times.
