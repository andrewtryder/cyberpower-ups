# Protocol & Reverse-Engineering Notes

Technical detail recovered from PowerPanel Personal’s native driver (`libppbedrvc.dylib`). This document is for contributors and anyone digging into the wire format. For build instructions and the public API, see [README.md](README.md).

---

## Confidence Legend

Every recovered constant in the headers is tagged:

| Tag | Meaning |
| --- | --- |
| **High** | Immediate evidence (cstring, cross-reference, or straight-line disassembly) |
| **Medium** | Layout is High; the English name is inferred |
| **Low** | Partial evidence |
| **Speculative** | Plausible, but not confirmed |

---

## What the Original Driver Is

`libppbedrvc.dylib` is the JNI native half of PowerPanel Personal.

It exports:

```text
Java_com_cyberpowersystems_ppbe_agent_core_converse_DriverTransaction_{start,stop,request}
```

and links against IOKit, CoreFoundation, and libstdc++.

Interesting C++ namespaces recovered from the binary:

| Namespace | Key classes |
| --- | --- |
| `cyberpower::ppbe::driver` | `Driver`, `Service`, `ProxyManager`, `Transaction` |
| `cyberpower::device::macosx` | `DeviceManagerImp`, `HidDeviceImp`, `SerialDeviceImp` |
| `cyberpower::hid::macosx` | `HidOperatorImp` |
| `cyberpower::rs232::macosx` | `SerialCommImp` |
| `cyberpower::ups::protocol` | `v1`, `v2e`, `v3`, `titan`, `modbus` |
| `cyberpower::ups::complete` | `HidUps`, `V1Ups`, `V2eUps`, `V3Ups`, `TitanUps` |

Event names found as strings (High confidence):

`IpcEventReceive`, `FeedbackConverseEvent`, `AppInitEvent`, `SynchronizeEvent`, `TrivialNumberedEvent`, `StopQueueEvent`, `TimerEvent`, `ReceiveConverseEvent`.

This library does **not** recreate the in-process event bus.

---

## Text Protocol (v1 / v2e / titan)

Commands are ASCII and terminated with CR (`0x0D`). `v2e::Responser::Delimiter()` and `titan::Responser::Delimiter()` both `return 0x0D` (High). v2e `ExactLength()` returns `-1` (variable).

### Serial settings

`SerialCommImp::Initialize` programs the port and then calls `cfsetispeed` / `cfsetospeed` with **2400** (High). `SetBaudrate` also accepts 1200, 4800, and 9600.

Default termios image:

| Field | Value | Meaning |
| --- | --- | --- |
| `c_iflag` | `5` | `IGNBRK \| IGNPAR` |
| `c_oflag` | `0` | — |
| `c_lflag` | `0` | raw |
| `c_cflag` | `0x8B00` | `CLOCAL \| CREAD \| CS8` — 8N1 |
| `VMIN` | `0` | — |

### Commands recovered from constructor string references (High)

| Command | Literal | Used by |
| --- | --- | --- |
| Status | `D\r` | v1 and v2e `StatusRequester` |
| Rating | `F\r` | v1 `FormularRequester`, titan `RatingInformationRequester` |
| Toggle buzzer | `B\r` | v1 `ToggleBuzzerRequester` |
| Quick battery test | `T\r` | v2e and titan |
| Cancel battery / self-test | `CT\r` | v1, v2e, titan |
| Calibrate | `TL\r` | v2e and titan |
| Cancel schedule | `C\r` | v2e and titan |
| Turn off/on fragment | `CS\r` | v1 `TurnOffOnRequester` |
| Indicator test | `TI\r` | v2e |
| Buzzer test | `TB\r` | v2e |
| Bypass current | `DN\r` | v2e |
| Bypass frequency | `DG\r` | v2e |
| Input frequency | `DF\r` | v2e |
| Bypass voltage | `DY\r` | v2e |
| Input voltage | `DI\r` | v2e |
| Titan status | `Q4\r` | titan `StatusRequester` |
| Titan model | `MD\r` | titan |
| Titan info | `I\r` | titan |
| Titan parameters | `QP\r` | titan |
| Titan fault | `QF\r` | titan |

Fragments, not complete commands (High that the literal is referenced, Low for the rest of the grammar): `CS:`, `RP`, `WI`, `C57:`.

### Status response (`D`)

`StatusResponser::operator()` requires length ≥ 3, a leading `#`, and a trailing CR (High). The body is repeating `<Tag><number>` tokens. The number charset is `.k0123456789`, or `.k0123456789abcdef` when the responser was constructed in hex mode (High).

`TurnToNumber` multiplies by **1000** (`0x3E8`). A trailing `k` multiplies by 1000 again (High). Tags `C` and `R` then compute `(scaled * 60) / 1000`, which is minutes converted to seconds if the field is in minutes. The arithmetic is High; the word “minutes” is Medium.

Switch cases recovered from the jump table (letter and stored offset are High; the name is Medium):

| Tag | Object offset | Stored as | Medium name |
| --- | --- | --- | --- |
| `I` | `0x40` | nominal × 1000 | input voltage |
| `O` | `0x44` | nominal × 1000 | output voltage |
| `Y` | `0x48` | nominal × 1000 | (unnamed) |
| `L` | `0x4c` | nominal × 1000 | load percent |
| `B` | `0x50` | nominal × 1000 | battery percent |
| `V` | `0x54` | nominal × 1000 | (unnamed) |
| `T` | `0x58` | nominal × 1000 | temperature |
| `F` | `0x5c` | nominal × 1000 | frequency |
| `H` | `0x60` | nominal × 1000 | battery voltage |
| `G` | `0x64` | nominal × 1000 | (unnamed) |
| `R` | `0x68` | seconds | runtime |
| `C` | `0x6c` | seconds | second time field |
| `Q` | `0x70` | nominal × 1000 | (unnamed) |
| `J` | `0x74` | nominal × 1000 | (unnamed) |
| `E` | `0x78` | nominal × 1000 | (unnamed) |
| `N` | `0x7c` | nominal × 1000 | (unnamed) |
| `P` | special | two-part / bitset parse | status bits |
| `W` | — | tail passed to `OutletStateResponser` | outlet state |

`P` and several bit masks inside the parser were not fully named. Unknown tags are kept on `Status::frame.fields`.

Empty input stores error `0xC8` (200, `RESP_ERR_EMPTY`). A bad frame stores `0xC9` (201, `RESP_ERR_FORMAT_ESSENTIAL`). Later paths store `0xD1` (209) and `0xD3` (211). Those four immediates fix the `RESP_ERR_*` enum at base 200 (High). The `UPS_ERR_*` names are High; their numeric base is Medium (cstring order, starting at 0).

---

## v3 Binary Protocol

`Crc8::Calc` is CRC-8, polynomial `0xD5`, init 0, no reflection (High). This is the same polynomial as CRC-8/DVB-S2.

`VerifyPacket` (High):

- length at least 3
- `(first_byte & 0x3F) == length - 2`
- bit 7 of the first byte must be clear
- bit 6 is a “last chunk” flag (`SendRequest` does `shl 6` of that flag)
- the last byte is the checksum

Handshake bytes: `0x40` / `0xC0`. `CheckHandShake` returns status 2 (`NO_HANDSHAKE`) otherwise (High). `WriteHandShake` writes that one byte. A short write stores status 5 (`WRITE_FAIL`).

---

## HID

USB vendor id **0x0764** (High). The enumerator builds matching dictionaries for product ids **0x0005**, **0x0501**, and **0x0601** (High). `UsageMapping::Initialize` branches on product id **0x051D** and maps a long table of HID elements (High). This library matches the whole vendor so the other model branches are still visible.

Usages used for the status snapshot (page/usage High; English names from the USB HID Usage Tables, Medium):

| Page | Usage | Name |
| --- | --- | --- |
| `0x84` | `0x30` | Voltage (parent `0x1A` input, `0x1C` output, `0x12` battery) |
| `0x84` | `0x32` | Frequency |
| `0x84` | `0x35` | PercentLoad |
| `0x84` | `0x36` | Temperature |
| `0x85` | `0x66` | RemainingCapacity |
| `0x85` | `0x67` | FullChargeCapacity |
| `0x85` | `0x68` | RunTimeToEmpty |
| `0x85` / `0xFF01` | `0xD0` | AC present |
| `0x85` | `0x44`, `0xD1` | Charging |
| `0x85` | `0x45`, `0xD2` | Discharging |

Vendor page `0xFF86` is mapped heavily (usages `0x72`, `0x42`, `0x16`, …). Those values are not decoded here; the semantic is Low.

The macOS backend opens the device with `IOHIDDeviceOpen`, registers an input-report callback so the element cache fills, and reads elements with `IOHIDDeviceGetValue` plus `IOHIDValueGetScaledValue`. If PowerPanel Personal already has the device seized, open fails.

---

## Models

Comparison strings found in the binary include:

`CPS1000EI`, `CPS2000EI`, `CPS600E`, `CPS1000E`, `CPS1500PIE`, `CPS3500PIE`, `CPS5000PIE`, `CPS7500PIE`, `CPS1500PRO`, `CPS3500PRO`, `CPS5000PRO`, `CPS7500PRO`, plus a longer OR / PR / OL / PP list.

Model-specific code in the original driver picks among v1, v2e, v3, titan, and HID usage tables. This library currently speaks **v2e `D`** on serial and **standard HID usages** on USB.

---

## Serial Port Names

Serial nodes the driver names explicitly include `cu.wchusbserial`. The CLI also lists `cu.usbserial`, `cu.usbmodem`, and `cu.SLAB` (Low; not spelled in the binary).

`src/platform/windows/transport_stub.cpp` compiles the same protocol against an empty enumerator. It does not open a handle.
