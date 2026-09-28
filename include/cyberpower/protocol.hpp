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

// Writable controls recovered from HidUps + UsageMapping (High page/usage).
// Report IDs are descriptor-owned; PID 0x0601 fallbacks are High from the
// live CP1500PFCLCDa report descriptor.
constexpr uint16_t kUsageAudibleAlarmControl = 0x005A;  // Power Device
constexpr uint16_t kUsageTest = 0x0058;                  // Power Device

// Audible Alarm Control values (High for 1..3; 4/5 when capability max > 3).
constexpr int kAlarmDisable = 1;
constexpr int kAlarmEnable = 2;
constexpr int kAlarmMute = 3;

// HidUps::TEST_MODE immediates from OnHandleConverse (Medium English names).
constexpr int kTestQuick = 1;
constexpr int kTestDeep = 2;
constexpr int kTestAbort = 3;

// Feature report ID fallbacks for product id 0x0601 only (High from descriptor).
constexpr uint8_t kPid0601AlarmReportId = 0x0C;
constexpr uint8_t kPid0601TestReportId = 0x14;
constexpr uint16_t kPidCp1500Pfclcda = 0x0601;

// Usages that both appear in UsageMapping::Initialize and have a standard
// USB HID Power Device / Battery System name. The pair (page, usage) is
// High. The English name is the HID Usage Tables name (Medium: the binary
// does not store the English string, but the numeric pair matches the spec).
struct HidUsage {
  uint16_t page;
  uint16_t usage;
  const char* name;  // HID Usage Tables name
};

// Subset used to build a status snapshot. All pairs were extracted from
// the mov-word immediates in UsageMapping::Initialize.
constexpr HidUsage kStatusUsages[] = {
    {0x0084, 0x0030, "Voltage"},
    {0x0084, 0x0032, "Frequency"},
    {0x0084, 0x0035, "PercentLoad"},
    {0x0084, 0x0036, "Temperature"},
    {0x0084, 0x0040, "ConfigVoltage"},
    {0x0084, 0x0042, "ConfigFrequency"},
    {0x0084, 0x001A, "Input"},
    {0x0084, 0x001C, "Output"},
    {0x0084, 0x0012, "Battery"},
    {0x0084, 0x0024, "PowerSummary"},
    {0x0084, 0x0004, "UPS"},
    {0x0085, 0x0066, "RemainingCapacity"},
    {0x0085, 0x0067, "FullChargeCapacity"},
    {0x0085, 0x0068, "RunTimeToEmpty"},
    {0x0085, 0x0044, "Charging"},
    {0x0085, 0x0045, "Discharging"},
    {0x0085, 0x00D0, "ACPresent"},       // also mapped on page 0xFF01
    {0x0085, 0x00D1, "BatteryCharging"},  // also mapped on page 0xFF01
    {0x0085, 0x00D2, "BatteryDischarging"},
    {0xFF01, 0x00D0, "ACPresentVendor"},
    {0xFF01, 0x00D1, "ChargingVendor"},
    {0xFF01, 0x00D2, "DischargingVendor"},
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
