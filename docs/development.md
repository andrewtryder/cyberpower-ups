# Development, testing, and project layout

For downloads and quick builds see the [README](../README.md). Contributor policies are in [CONTRIBUTING.md](../CONTRIBUTING.md).

## Make convenience targets

CMake is authoritative; the repository Makefile is primarily for macOS/Linux:

```bash
make                         # Configure + build
make test                    # Offline CTest and protocol self-test
make run                     # Read UPS status
make run ARGS='--json'
make monitor
make install                 # PREFIX=/usr/local by default
make help
```

Also available:

```bash
make BUILD_TYPE=Debug
make install PREFIX=/opt/local
make clean                   # Remove build outputs
make distclean               # Delete build directory
```

Use `cmake --build build --config Release` with a multi-configuration generator such as MSVC.

## Offline tests

No UPS is required:

```bash
cmake -S . -B build -DCYBERPOWER_ENABLE_WARNINGS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
./build/cpups --self-test    # macOS/Linux
```

Tests cover serial frames and invalid inputs, HID decoding, protocol fixtures, CMake version synchronization, and POSIX pseudo-terminals. CI checks CMake and pkg-config consumer projects under `tests/consumer-cmake/` and `tests/consumer-pkgconfig/`.

### Sanitizers

On supported GCC/Clang toolchains:

```bash
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCYBERPOWER_ENABLE_WARNINGS=ON -DCYBERPOWER_ENABLE_SANITIZERS=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
./build-asan/cpups --self-test
```

Use `make check-format` and `make lint` for local quality gates as described in [CONTRIBUTING.md](../CONTRIBUTING.md).

## Fixtures and sanitized captures

Protocol fixtures and capture guidance are in [fixtures/README.md](../fixtures/README.md). Optional real-device examples:

```bash
./build/cpups --json > fixtures/cp1500pfclcda_status.json
./build/cpups --rating > fixtures/cp1500pfclcda_rating.txt
./build/cpups --dump-raw 2> fixtures/cp1500pfclcda_raw.txt
```

`--rating` requires supported serial hardware; `--dump-raw` writes to stderr. Remove device serial numbers, battery lot numbers, and host identifiers before committing or sharing. Never run disruptive hardware operations in CI.

## Project layout

```text
cyberpower-ups/
├── include/cyberpower/     Public C and C++ headers
├── src/
│   ├── platform/macos/    IOKit HID and POSIX serial
│   ├── platform/linux/    hidraw HID and POSIX serial
│   └── platform/windows/  Stub transport
├── tools/                  cpups CLI
├── examples/               Library examples
├── tests/                  Offline tests and consumers
├── fixtures/               Capture instructions and data
├── cmake/                  CMake/pkg-config templates
├── docs/                   Usage, platforms, development, policies
├── PROTOCOL.md             Reverse-engineering notes
├── CMakeLists.txt           Authoritative build
└── Makefile                 Convenience targets
```

See [Protocol](../PROTOCOL.md), [Versioning](versioning.md), and [Governance](governance.md) for deeper detail.
