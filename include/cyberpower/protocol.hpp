#pragma once

// Recovered wire protocol from PowerPanel Personal's libppbedrvc.dylib
// (x86_64, PowerPanel_Native). Every constant is tagged:
//   High        - immediate, cstring xref, or straight-line disassembly
//   Medium      - structure is High, the English name is inferred
//   Low         - partial evidence
//   Speculative - plausible, not nailed down in this binary

#include "cyberpower/errors.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace cyberpower::protocol {

// ---------------------------------------------------------------------------
// Text protocols (v1, v2e, titan) share CR framing.
// ---------------------------------------------------------------------------

// v2e::Responser::Delimiter() and titan::Responser::Delimiter() return 0x0D.
// High.
constexpr char kFrameDelimiter = '\r';

// v2e status body must be at least 3 bytes, start with '#', end with CR.
// High (StatusResponser::operator()).
constexpr char kStatusLead = '#';
constexpr int kStatusMinBytes = 3;

// TurnToNumber multiplies the parsed number by this scale (0x3E8).
// A trailing 'k'/'K' multiplies once more by the same scale. High.
constexpr int kNumberScale = 1000;

// Fields 'C' and 'R' then do (scaled * 60) / 1000, i.e. minutes -> seconds
// if the field is expressed in minutes. The arithmetic is High. Calling the
// input "minutes" is Medium (no unit string sits next to the multiply).
constexpr int kMinutesToSecondsNum = 60;
constexpr int kMinutesToSecondsDen = 1000;

// SerialCommImp::Initialize() does cfsetispeed/cfsetospeed(2400) after
// programming termios. High.
constexpr int kDefaultBaud = 2400;

// SetBaudrate accepts exactly these four. High (compared immediates).
constexpr int kSupportedBauds[] = {1200, 2400, 4800, 9600};

// termios programmed in SerialCommImp::Initialize. High.
// c_iflag = 5 = IGNBRK|IGNPAR, c_oflag = 0, c_lflag = 0,
// c_cflag = 0x8B00 = CLOCAL|CREAD|CS8, VMIN = 0.
constexpr unsigned long kTermiosIflag = 5;
constexpr unsigned long kTermiosCflag = 0x8B00;

// ---------------------------------------------------------------------------
// Commands whose literal is loaded by a requester constructor. High,
// unless noted. The string includes the CR when the constructor did.
// ---------------------------------------------------------------------------

namespace cmd {

// v1 and v2e both construct StatusRequester with "D\r". High.
constexpr const char* kStatus = "D\r";

// v1::FormularRequester and titan::RatingInformationRequester. High.
constexpr const char* kRating = "F\r";

// v1::ToggleBuzzerRequester. High.
constexpr const char* kToggleBuzzer = "B\r";

// v1::SelftestRequester and v2e/titan CancelBatteryTestRequester. High.
constexpr const char* kCancelOrSelfTest = "CT\r";

// v1::TurnOffOnRequester also references "CS\r". High as a literal;
// how the (int,int) arguments are appended is Medium (not fully traced).
constexpr const char* kTurnOffOn = "CS\r";

// v2e queries. High.
constexpr const char* kBypassCurrent = "DN\r";
constexpr const char* kBypassFrequency = "DG\r";
constexpr const char* kInputFrequency = "DF\r";
constexpr const char* kBypassVoltage = "DY\r";
constexpr const char* kInputVoltage = "DI\r";
constexpr const char* kCancelSchedule = "C\r";
constexpr const char* kIndicatorTest = "TI\r";
constexpr const char* kBuzzerTest = "TB\r";
constexpr const char* kBatteryCalibrate = "TL\r";
constexpr const char* kBatteryTestQuick = "T\r";

// Parameterized fragments (not a complete command by themselves). High
// that the constructor references the literal; Low for the full grammar.
constexpr const char* kCancelSchedulePrefix = "CS:";
constexpr const char* kQueryMemoryPrefix = "RP";
constexpr const char* kTurnOnImmediatePrefix = "WI";
constexpr const char* kConfigBatteryPackPrefix = "C57:";

// titan-only. High.
constexpr const char* kTitanUpsInfo = "I\r";
constexpr const char* kTitanModel = "MD\r";
constexpr const char* kTitanParameter = "QP\r";
constexpr const char* kTitanFault = "QF\r";
constexpr const char* kTitanStatus = "Q4\r";

}  // namespace cmd

// ---------------------------------------------------------------------------
// v2e "D" response. Body is '#' + repeating <Tag><number>[k] + CR.
// Tag charset stop-set is ".k0123456789" (decimal) or
// ".k0123456789abcdef" (hex). High.
//
// Stored integer for a normal field is nominal * 1000.
// Stored integer for 'C' and 'R' is nominal_minutes * 60 (seconds).
// ---------------------------------------------------------------------------

struct FieldValue {
  bool present = false;
  // Integer the driver stores (scaled, and converted for C/R).
  int32_t stored = 0;
  // Substring that belonged to the tag, without the tag letter.
  std::string token;
};

struct StatusFrame {
  Error error = Error::Ok;
  bool ok = false;
  std::string raw;
  // Keyed by the tag letter. High that these letters are the switch cases.
  std::map<char, FieldValue> fields;
  // 'W' tail is handed to OutletStateResponser. High that 'W' is special.
  std::string outlet_state;
};

// hex_numbers selects the ".k0123456789abcdef" charset (StatusResponser's
// bool constructor argument). High that the flag switches the charset.
StatusFrame parse_v2e_status(const std::string& frame, bool hex_numbers = false);

// 0x85/0x67 is Temperature in RE-confirmed modern descriptors, but older
// firmware used it for FullChargeCapacity. A descriptor whose logical maximum
// exceeds a plausible temperature range is treated as the legacy capacity.
// Medium: compatibility rule based on descriptor metadata, not value alone.
bool battery_usage_67_is_legacy_full_capacity(int logical_min, int logical_max);

// Nominal (divide stored by 1000) for ordinary fields. For 'C' and 'R',
// `stored` is already seconds.
double nominal_from_stored(char tag, int32_t stored);

// ---------------------------------------------------------------------------
// v3 binary packets. High unless noted.
//
// Handshake byte written by WriteHandShake, and accepted by CheckHandShake:
//   0x40 and 0xC0. Anything else yields V3Status::NoHandshake (2).
//
// Request chunk header (low 6 bits = payload length, bit 6 = last chunk):
//   header = (length & 0x3F) | (last ? 0x40 : 0)
//   bit 7 must be clear or VerifyPacket rejects the packet.
// Then `length` payload bytes, then one checksum byte.
// Checksum is CRC-8, poly 0xD5, init 0, no reflection, xorout 0
// (Crc8::Calc). The byte is appended after the checksum object returns.
// ---------------------------------------------------------------------------

constexpr uint8_t kV3HandshakeA = 0x40;  // High
constexpr uint8_t kV3HandshakeB = 0xC0;  // High
constexpr uint8_t kV3LengthMask = 0x3F;  // High
constexpr uint8_t kV3LastChunkBit = 0x40;  // High (shl 6 of the "last" flag)
constexpr uint8_t kCrc8Poly = 0xD5;  // High

uint8_t crc8(const uint8_t* data, std::size_t len);
uint8_t crc8(const std::string& data);

struct V3Packet {
  bool last = true;
  std::string payload;
  uint8_t checksum = 0;
};

// Encode one chunk. length must be 0..63. Returns empty on overflow.
std::string encode_v3_chunk(const std::string& payload, bool last);

// Decode one chunk from a buffer that already includes header and checksum.
// On failure, status is PayloadFormatFail or ChecksumFail.
bool decode_v3_chunk(const uint8_t* data, std::size_t len, V3Packet& out, V3Status& status);

// ---------------------------------------------------------------------------
// HID. Vendor 0x0764 is CyberPower. High (mov edi, 0x764 next to product ids).
// Matching dictionaries are built for product ids 0x0005, 0x0501, 0x0601.
// UsageMapping additionally branches on product id 0x051D. High.
// ---------------------------------------------------------------------------

constexpr uint16_t kVendorId = 0x0764;
constexpr uint16_t kKnownProductIds[] = {0x0005, 0x0501, 0x0601, 0x051D};

// Usage pages seen as the high word of each mapped element. High.
constexpr uint16_t kPagePowerDevice = 0x0084;
constexpr uint16_t kPageBattery = 0x0085;
constexpr uint16_t kPageVendorFf86 = 0xFF86;
constexpr uint16_t kPageVendorFf01 = 0xFF01;

// ---------------------------------------------------------------------------
// Power Device (0x84) usages recovered from UsageMapping::Initialize. High.
// ---------------------------------------------------------------------------

// Writable controls (High page/usage). Report IDs come from the descriptor;
// these are HID usages, NOT report IDs — they are distinct.
constexpr uint16_t kUsageAudibleAlarmControl = 0x005A;  // 0x84/0x5A
constexpr uint16_t kUsageTest = 0x0058;                  // 0x84/0x58

// Read-only status usages (High).
constexpr uint16_t kUsageVoltage        = 0x0030;  // Voltage (parent coll selects I/O/bat)
constexpr uint16_t kUsageFrequency      = 0x0032;  // Frequency
constexpr uint16_t kUsagePercentLoad    = 0x0065;  // PercentLoad — High from UsageMapping
constexpr uint16_t kUsageTemperaturePD  = 0x0036;  // Temperature (Power Device page)
constexpr uint16_t kUsageACPresent      = 0x00FD;  // ACPresent (also on 0xFF01/0xD0)
constexpr uint16_t kUsageFirmwareVer    = 0x00FE;  // Firmware version string / iProduct

// Audible Alarm Control values (High for 1..3).
constexpr int kAlarmDisable = 1;
constexpr int kAlarmEnable = 2;
constexpr int kAlarmMute = 3;

// HidUps::TEST_MODE immediates (Medium English names).
constexpr int kTestQuick = 1;
constexpr int kTestDeep = 2;
constexpr int kTestAbort = 3;

// Feature report ID fallbacks for product id 0x0601 only (High from descriptor).
constexpr uint8_t kPid0601AlarmReportId = 0x0C;
constexpr uint8_t kPid0601TestReportId = 0x14;
constexpr uint16_t kPidCp1500Pfclcda = 0x0601;

// ---------------------------------------------------------------------------
// Battery System (0x85) usages recovered from UsageMapping::Initialize. High.
// ---------------------------------------------------------------------------

constexpr uint16_t kUsageRemainingCapacity    = 0x002C;  // RemainingCapacity (percent or raw)
constexpr uint16_t kUsageRunTimeToEmpty       = 0x008B;  // RunTimeToEmpty (seconds)
constexpr uint16_t kUsageFullChargeCapacity   = 0x008D;  // FullChargeCapacity
constexpr uint16_t kUsageDesignCapacity       = 0x008E;  // DesignCapacity
constexpr uint16_t kUsageCycleCount           = 0x008C;  // CycleCount
constexpr uint16_t kUsageTemperatureBat       = 0x0067;  // Temperature (Battery page)
constexpr uint16_t kUsageNeedReplacement      = 0x0029;  // NeedReplacement (bit)
constexpr uint16_t kUsageCharging             = 0x0068;  // Charging (bit)
constexpr uint16_t kUsageDischarging          = 0x0066;  // Discharging (bit)

// Older / alternative Battery System usages that may appear on some firmware
// revisions (present in prior mapping, kept for compatibility). Medium.
constexpr uint16_t kUsageRemCapAlt  = 0x0066;  // RemainingCapacity (alternate, pre-RE)
constexpr uint16_t kUsageFullCapAlt = 0x0067;  // FullChargeCapacity (alternate)
constexpr uint16_t kUsageRuntimeAlt = 0x0068;  // RunTimeToEmpty (alternate)
constexpr uint16_t kUsageChargingAlt  = 0x0044;  // Charging (alternate / older firmware)
constexpr uint16_t kUsageDischargingAlt = 0x0045;  // Discharging (alternate)
constexpr uint16_t kUsageACPresentAlt   = 0x00D0;  // ACPresent (Battery / 0xFF01 pages)
constexpr uint16_t kUsageChargingAlt2   = 0x00D1;  // Charging (0xFF01)
constexpr uint16_t kUsageDischargingAlt2 = 0x00D2; // Discharging (0xFF01)

// ---------------------------------------------------------------------------
// Vendor page 0xFF86 — CyberPower proprietary config usages. High page/usage;
// confidence tags on individual usages noted below.
// IMPORTANT: these are HID usage numbers, NOT HID report IDs. Report IDs for
// these usages on PID 0x0601 must be read from the device descriptor at
// runtime via IOHIDElementGetReportID — do not hardcode them.
// ---------------------------------------------------------------------------

// Voltage sensitivity (input transfer threshold preset).
//   Read : page 0xFF86 / usage 0x61 (High — extracted from Initialize movw).
//   Write: page 0xFF86 / usage 0x72 (High — SetupVoltageSensitivity path).
//   Values: 1=High sensitivity, 2=Medium, 3=Low (Medium — inferred from
//           3-value alarm pattern; not confirmed by disasm alone).
constexpr uint16_t kUsageVendorSensitivityRead  = 0x0061;
constexpr uint16_t kUsageVendorSensitivityWrite = 0x0072;
constexpr int kSensitivityHigh   = 1;  // Medium confidence on value encoding
constexpr int kSensitivityMedium = 2;
constexpr int kSensitivityLow    = 3;

// Shutdown / restore delay timers. Units: seconds (High — HidUps::Sleep(int)
// confirmed to pass seconds directly).
//   Shutdown delay read/write : page 0xFF86 / usage 0x16 (High).
//   Restore/startup delay r/w : page 0xFF86 / usage 0x52 (High).
constexpr uint16_t kUsageVendorShutdownDelay = 0x0016;
constexpr uint16_t kUsageVendorRestoreDelay  = 0x0052;

// Second config-write slot paired with sensitivity write in Initialize (High
// that both appear, Low for the full semantic). Not currently used for writes.
constexpr uint16_t kUsageVendorConfigParam = 0x0042;

// Usages that both appear in UsageMapping::Initialize and have a standard
// USB HID Power Device / Battery System name. The pair (page, usage) is
// High. The English name is the HID Usage Tables name (Medium: the binary
// does not store the English string, but the numeric pair matches the spec).
struct HidUsage {
  uint16_t page;
  uint16_t usage;
  const char* name;  // HID Usage Tables name
};

// Usages used to build a status snapshot.
// Column 1: page. Column 2: usage. Column 3: USB HID name (Medium for name;
// High for the (page,usage) pair extracted from UsageMapping::Initialize).
constexpr HidUsage kStatusUsages[] = {
    // Power Device — structural / collection usages
    {0x0084, 0x0004, "UPS"},
    {0x0084, 0x0012, "Battery"},
    {0x0084, 0x001A, "Input"},
    {0x0084, 0x001C, "Output"},
    {0x0084, 0x0024, "PowerSummary"},
    {0x0084, 0x0040, "ConfigVoltage"},
    {0x0084, 0x0042, "ConfigFrequency"},
    // Power Device — numeric readings
    {0x0084, 0x0030, "Voltage"},
    {0x0084, 0x0032, "Frequency"},
    {0x0084, 0x0035, "PercentLoad"},    // older firmware; 0x65 is RE-confirmed
    {0x0084, 0x0065, "PercentLoad"},    // High — confirmed in UsageMapping::Initialize
    {0x0084, 0x0036, "Temperature"},
    // Power Device — status bits (RE-confirmed from Initialize)
    {0x0084, 0x00FD, "ACPresent"},      // High
    {0x0084, 0x00FE, "FirmwareVersion"},// High — iProduct / string usage
    // Battery System — RE-confirmed primary usages from UsageMapping
    {0x0085, 0x002C, "RemainingCapacity"},   // High
    {0x0085, 0x008B, "RunTimeToEmpty"},      // High
    {0x0085, 0x008C, "CycleCount"},          // High
    {0x0085, 0x008D, "FullChargeCapacity"},  // High
    {0x0085, 0x008E, "DesignCapacity"},      // High
    {0x0085, 0x0067, "Temperature"},         // High — Battery System temperature
    {0x0085, 0x0029, "NeedReplacement"},     // High
    {0x0085, 0x0068, "Charging"},            // High (also 0x0044 on older fw)
    {0x0085, 0x0066, "Discharging"},         // High (also 0x0045 on older fw)
    // Battery System — alternate usages (older firmware / kept for compat)
    {0x0085, 0x0066, "RemainingCapacityAlt"},
    {0x0085, 0x0067, "FullChargeCapacityAlt"},
    {0x0085, 0x0068, "RunTimeToEmptyAlt"},
    {0x0085, 0x0044, "Charging"},
    {0x0085, 0x0045, "Discharging"},
    {0x0085, 0x00D0, "ACPresent"},           // also on 0xFF01
    {0x0085, 0x00D1, "BatteryCharging"},     // also on 0xFF01
    {0x0085, 0x00D2, "BatteryDischarging"},
    {0xFF01, 0x00D0, "ACPresentVendor"},
    {0xFF01, 0x00D1, "ChargingVendor"},
    {0xFF01, 0x00D2, "DischargingVendor"},
    // Vendor 0xFF86 — config / delay usages
    {0xFF86, 0x0061, "VoltageSensitivity"}, // High — read
    {0xFF86, 0x0016, "ShutdownDelay"},      // High
    {0xFF86, 0x0052, "RestoreDelay"},       // High
};

// ---------------------------------------------------------------------------
// Event names recovered as cstrings. High as names. Delivery is an internal
// queue (IpcReceiveEvent, FeedbackConverseEvent, TimerEvent); this library
// does not recreate the JNI/IPC bus.
// ---------------------------------------------------------------------------

constexpr const char* kEventNames[] = {
    "IpcEventReceive",
    "FeedbackConverseEvent",
    "AppInitEvent",
    "SynchronizeEvent",
    "TrivialNumberedEvent",
    "StopQueueEvent",
    "TimerEvent",
    "ReceiveConverseEvent",
};

// Model name strings compiled into the driver. High that the driver compares
// against them. Not a complete catalog (the cstring table is much longer).
constexpr const char* kModelNames[] = {
    "CPS1000EI", "CPS2000EI", "CPS600E",   "CPS1000E",   "CPS1500PIE",
    "CPS3500PIE", "CPS5000PIE", "CPS7500PIE", "CPS1500PRO", "CPS3500PRO",
    "CPS5000PRO", "CPS7500PRO",
};

// Parser self-check used by the example. Returns false if a recovered rule
// regresses. Does not need hardware.
bool self_test(std::string* detail);

}  // namespace cyberpower::protocol
