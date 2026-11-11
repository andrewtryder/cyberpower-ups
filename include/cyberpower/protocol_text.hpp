#pragma once

// Text protocols (v1 / v2e / titan): command literals and the v2e status parser.
// Provenance tags (High / Medium / Low / Speculative) are defined in protocol.hpp.

#include "cyberpower/errors.hpp"

#include <cstdint>
#include <map>
#include <string>

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

}  // namespace cyberpower::protocol
