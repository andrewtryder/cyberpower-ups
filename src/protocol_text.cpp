#include "cyberpower/protocol_text.hpp"

#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <sstream>

namespace cyberpower::protocol {
namespace {

bool is_digit_for(char c, bool hex) {
  if (c >= '0' && c <= '9') return true;
  if (c == '.' || c == 'k' || c == 'K') return true;
  if (!hex) return false;
  return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

int32_t turn_to_number(const std::string& token, bool hex, bool* ok) {
  // Mirrors helper::TurnToNumber: integer path multiplies by the scale,
  // decimal path multiplies the double by the scale and truncates.
  std::string body = token;
  bool kilo = false;
  if (!body.empty() && (body.back() == 'k' || body.back() == 'K')) {
    kilo = true;
    body.pop_back();
  }
  if (body.empty()) {
    *ok = false;
    return 0;
  }
  int64_t scaled = 0;
  if (body.find('.') == std::string::npos) {
    int64_t value = 0;
    const auto parsed = std::from_chars(body.data(), body.data() + body.size(), value,
                                        hex ? 16 : 10);
    if (parsed.ec != std::errc{} || parsed.ptr != body.data() + body.size()) {
      *ok = false;
      return 0;
    }
    if (value > std::numeric_limits<int64_t>::max() / kNumberScale) {
      *ok = false;
      return 0;
    }
    scaled = value * kNumberScale;
  } else {
    // A decimal token needs digits on both sides of its single decimal point.
    // strtod alone would accept incomplete tokens such as "1.".
    if (hex || body.find('.') != body.rfind('.') || body.front() == '.' || body.back() == '.') {
      *ok = false;
      return 0;
    }
    errno = 0;
    char* consumed = nullptr;
    const double value = std::strtod(body.c_str(), &consumed);
    if (errno == ERANGE || consumed != body.c_str() + body.size() || !std::isfinite(value) ||
        value > static_cast<double>(std::numeric_limits<int32_t>::max()) / kNumberScale) {
      *ok = false;
      return 0;
    }
    scaled = static_cast<int64_t>(std::trunc(value * static_cast<double>(kNumberScale)));
  }
  if (kilo) {
    if (scaled > std::numeric_limits<int32_t>::max() / kNumberScale) {
      *ok = false;
      return 0;
    }
    scaled *= kNumberScale;
  }
  if (scaled > std::numeric_limits<int32_t>::max()) {
    *ok = false;
    return 0;
  }
  *ok = true;
  return static_cast<int32_t>(scaled);
}

int32_t minutes_scaled_to_seconds(int32_t scaled) {
  // (scaled * 60) / 1000, matching the magic-number divide in the parser.
  return static_cast<int32_t>(
      (static_cast<int64_t>(scaled) * kMinutesToSecondsNum) / kMinutesToSecondsDen);
}

}  // namespace

StatusFrame parse_v2e_status(const std::string& frame, bool hex_numbers) {
  StatusFrame out;
  out.raw = frame;
  if (frame.empty()) {
    out.error = Error::RespEmpty;  // 0xC8
    return out;
  }
  if (frame.size() < static_cast<std::size_t>(kStatusMinBytes) ||
      frame.front() != kStatusLead || frame.back() != kFrameDelimiter) {
    out.error = Error::RespFormatEssential;  // 0xC9
    return out;
  }

  std::size_t i = 1;
  const std::size_t end = frame.size() - 1;  // exclude CR
  bool any = false;
  while (i < end) {
    const char tag = frame[i];
    if (is_digit_for(tag, hex_numbers)) {
      out.error = Error::RespFormatEssential;
      return out;
    }
    ++i;
    if (tag == 'W') {
      // The driver slices from 'W' to the end and hands it to
      // OutletStateResponser. Keep the tail, including nothing after 'W'.
      out.outlet_state = frame.substr(i, end - i);
      any = true;
      break;
    }
    const std::size_t start = i;
    while (i < end && is_digit_for(frame[i], hex_numbers)) ++i;
    FieldValue field;
    field.token = frame.substr(start, i - start);
    bool ok = false;
    int32_t stored = turn_to_number(field.token, hex_numbers, &ok);
    if (field.token.empty() || !ok) {
      out.error = Error::RespNotNumber;
      return out;
    }
    if (tag == 'C' || tag == 'R') stored = minutes_scaled_to_seconds(stored);
    field.stored = stored;
    field.present = true;
    out.fields[tag] = std::move(field);
    any = true;
  }
  if (!any) {
    out.error = Error::RespNoAvailableItem;  // 0xD3
    return out;
  }
  out.ok = true;
  out.error = Error::Ok;
  return out;
}

double nominal_from_stored(char tag, int32_t stored) {
  if (tag == 'C' || tag == 'R') return static_cast<double>(stored);
  return static_cast<double>(stored) / static_cast<double>(kNumberScale);
}

}  // namespace cyberpower::protocol
