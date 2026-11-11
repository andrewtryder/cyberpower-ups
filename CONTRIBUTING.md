# Contributing to cyberpower-ups

Thank you for contributing! This document details code quality standards and automated checks used across the repository.

---

## 1. Pull Request Titles (Conventional Commits)

All pull request titles must conform to the [Conventional Commits](https://www.conventionalcommits.org/) specification to ensure clean history and compatible squash-merges:

```
<type>[(<scope>)][!]: <description>
```

### Allowed Types
- `feat`: A new feature or capability
- `fix`: A bug fix or transport hardening
- `docs`: Documentation improvements
- `style`: Formatting or cosmetic changes (no code logic change)
- `refactor`: Code refactoring without behavioral changes
- `perf`: Performance optimizations
- `test`: Test suite additions or corrections
- `build`: Build system or dependency updates (CMake, Makefile)
- `ci`: CI/CD workflows and automated quality gates
- `chore`: Maintenance tasks
- `revert`: Reverting a previous commit

### Examples
- `feat: add a new status field`
- `fix(linux): correct HID report parsing`
- `ci: introduce automated code quality gates`
- `docs: document Linux device permissions`
- `refactor(serial): consolidate POSIX transport logic`
- `feat!: change a public API contract`

You can test a PR title locally using:
```bash
./scripts/check-pr-title.py "feat(linux): my title"
```

---

## 2. Code Formatting (`clang-format`)

C and C++ files follow the `.clang-format` configuration (Google-based C++17 style with 2-space indentation and 100-column limit). The standardized version used in CI is **21.1.8** (`pip install clang-format==21.1.8` or via your package manager).

Enforcement in CI is **incremental**: only the lines and files modified in your pull request relative to `origin/main` are checked across `include`, `src`, `tools`, `examples`, and `tests`. This prevents unrelated formatting churn across the existing codebase while ensuring newly authored or edited lines conform to style.

### Check Formatting Locally
```bash
make check-format
# or directly:
./scripts/check-format.sh origin/main
python3 tests/test_check_format.py
```

### Automatically Format Changed Lines
```bash
make format
# or directly:
git-clang-format origin/main
```

---

## 3. GitHub Actions Workflow Linting (`actionlint`)

All workflows in `.github/workflows/` are checked by [actionlint](https://github.com/rhysd/actionlint).

To run actionlint locally:
```bash
# macOS (via Homebrew)
brew install actionlint

# Run check
actionlint
```

---

## 4. GitHub Actions Security Scanning (`zizmor`)

Workflows are audited for supply-chain security, credential exposure, and least-privilege permissions using [zizmor](https://github.com/zizmorcore/zizmor).

To run zizmor locally:
```bash
# Via cargo or Homebrew
brew install zizmor
# or: cargo install zizmor

# Run audit
zizmor .
```

---

## 5. Running All Quality Checks
```bash
make test          # Offline CTest suite + protocol self-test
make check-format  # clang-format check on changed lines + regression test suite
make lint          # actionlint and zizmor workflow audit
```

### Running with Sanitizers (AddressSanitizer + UndefinedBehaviorSanitizer)
To test for memory safety, out-of-bounds accesses, leaks, and undefined behavior locally:
```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCYBERPOWER_ENABLE_WARNINGS=ON -DCYBERPOWER_ENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
./build-asan/cpups --self-test
```
This is also run automatically on Linux in GitHub Actions CI.

---

## 6. Reporting Issues & Hardware Diagnostics

When filing bug reports or requesting device support, providing accurate hardware metadata is essential:

### Required Context
- **Device Model**: Exact model name from the device label (e.g., `CP1500PFCLCDa`).
- **USB VID/PID**: Hexadecimal Vendor ID and Product ID (e.g., `0764:0601`).
  - On macOS: `system_profiler SPUSBDataType` or `ioreg -p IOUSB -l`
  - On Linux: `lsusb`
- **Operating System & Architecture**: e.g., Ubuntu 24.04 (arm64 under UTM VM), macOS 14.5 (Apple Silicon M2).
- **Transport Mode**: USB HID (`/dev/hidrawX` or IOKit) or Serial (`/dev/ttyX` or `/dev/cu.usbserial-*`).
- **Reproduction Steps**: Exact CLI commands invoked or API calls made.

### Sanitizing Captures & Fixtures
When sharing diagnostic outputs (e.g., `cpups --json`, `cpups --dump-raw 2> capture.txt`):
> [!IMPORTANT]
> **Always sanitize your logs before posting publicly.**
> Remove device serial numbers, battery lot numbers, MAC addresses, and any local host identification. Check for fields like `"serial_number"` or raw text containing device serial codes and replace them with placeholder values (e.g., `XXXXXXXXXXXX`).

---

## 7. Pull Requests & Development Practices

We welcome contributions of all kinds — from core code to documentation, tests, and build tooling.

### Focused, Testable Changes
- Keep pull requests focused on a single logical change or feature.
- Avoid mixing formatting churn, unrelated refactors, or administrative changes with functional bug fixes.
- PR titles must adhere to Conventional Commits (see Section 1).

### Offline Testing & CI Safety
- **No Physical Hardware Required for Contributions**: You do **not** need physical CyberPower hardware to contribute. Bug fixes, parser logic, build configurations, and documentation can all be validated with offline tests.
- **Offline Protocol Regression Tests**: When fixing a protocol parsing bug or adding report handling, always add an offline unit test in [`tests/offline_test.cpp`](tests/offline_test.cpp) using hardcoded frame data or mock buffers. The offline suite tests boundary conditions, corrupted checksums, truncated frames, extreme values, and HID feature decoding.
- **No Hardware Actions in CI**: CI runs on GitHub-hosted public runners without attached UPS hardware. All tests in CI must execute completely offline without hardware side effects.
- **Cross-Platform & Sanitizer Coverage**: CI validates clean builds and tests across Ubuntu (standard release + ASan/UBSan sanitizers), macOS, and Windows (portable parser tests and clean compilation with MSVC).

### Hardware Compatibility Claims
- Claims of compatibility with specific UPS models must be backed by real physical verification or clearly qualified evidence (noting the exact hardware model, firmware version, and OS environment).
- Never infer compatibility with other CyberPower models solely from a shared USB Vendor ID (`0x0764`).

### Review & Merge Conventions
- All pull requests must pass the six required automated checks before merge:
  1. `Ubuntu build + self-test`
  2. `macOS build + self-test`
  3. `Actionlint`
  4. `Conventional PR title`
  5. `Clang-format (incremental)`
  6. `Zizmor security audit`
- Pull requests are squash-merged into `main` by the maintainer using a Conventional Commit squash title.

---

## 8. Release Automation & Versioning (Release Please)

Releases and changelogs are managed via automated [Release Please](https://github.com/googleapis/release-please) workflows following [Semantic Versioning](docs/versioning.md):

1. **Version Calculation**: Conventional Commit titles determine version increments (`fix:` -> patch, `feat:` -> minor, breaking changes during `0.x` -> minor).
2. **Release PR Creation**: When release-worthy commits merge into `main`, Release Please automatically opens or updates a release PR (`chore(main): release X.Y.Z`) modifying `CMakeLists.txt`, `version.txt`, `.release-please-manifest.json`, and `CHANGELOG.md`.
3. **Protected CI Checks**: Release PRs must pass the 6 required branch protection checks like any other PR.
4. **Maintainer Review & Merge**: The maintainer reviews and squash-merges the release PR into `main`.
5. **Release Publication**: Merging the release PR triggers tag creation (`vX.Y.Z`) and GitHub Release publication with formatted release notes.
6. **Local Version Checks**: Run `python3 scripts/check-version-sync.py` or `make test` to verify version consistency across all managed files.
7. **Troubleshooting & Recovery**: If release automation is halted (e.g. unconfigured `RELEASE_PLEASE_TOKEN` secret), configure the secret and trigger the `Release Please` workflow via `workflow_dispatch` on GitHub Actions. Release Please is idempotent and safely reconciles the release PR without manual tag manipulation.
