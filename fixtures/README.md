# Fixtures

Captured outputs from a real CyberPower UPS help validate parsing without attaching hardware in CI.

## Capture from a live device

With a UPS connected (and PowerPanel Personal quit so it is not seizing the HID device):

```bash
cmake -S . -B build && cmake --build build

# Structured status (preferred for offline JSON checks)
./build/cpups --json > fixtures/cp1500pfclcda_status.json

# Rating reply (serial protocols; HID returns NotSupported)
./build/cpups --rating > fixtures/cp1500pfclcda_rating.txt

# Raw wire capture while reading status (HID report hex / serial TX+RX)
./build/cpups --dump-raw 2> fixtures/cp1500pfclcda_raw.txt
./build/cpups --json --dump-raw > fixtures/cp1500pfclcda_status.json 2> fixtures/cp1500pfclcda_raw.txt
```

Name files after the product string or model when possible (`cpups` prints `product` in `--json`).

## Checked in

| File | Purpose |
|------|---------|
| `synthetic_status.json` | Minimal hand-written `cpups --json` sample for offline tests |
| `cp1500pfclcda_*.json/txt` | Optional real-device dumps (gitignored by default; add locally) |

Real-device filenames matching `fixtures/cp*` are gitignored so serial numbers are not committed by accident. Offline tests always run the protocol self-test and hard-coded v2e frames. JSON fixtures are loaded when present (`CPUPS_FIXTURES_DIR` points at this directory under CTest).
