#include "cyberpower/protocol.hpp"

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

bool battery_usage_67_is_legacy_full_capacity(int logical_min, int logical_max) {
  return logical_min >= 0 && logical_max > 200;
}

double nominal_from_stored(char tag, int32_t stored) {
  if (tag == 'C' || tag == 'R') return static_cast<double>(stored);
  return static_cast<double>(stored) / static_cast<double>(kNumberScale);
}

uint8_t crc8(const uint8_t* data, std::size_t len) {
  // Crc8::Calc: crc ^= byte; eight times shift left, xor 0xD5 if bit 7 was set.
  uint8_t crc = 0;
  for (std::size_t i = 0; i < len; ++i) {
    crc = static_cast<uint8_t>(crc ^ data[i]);
    for (int bit = 0; bit < 8; ++bit) {
      const uint8_t shifted = static_cast<uint8_t>(crc << 1);
      crc = (crc & 0x80) ? static_cast<uint8_t>(shifted ^ kCrc8Poly) : shifted;
    }
  }
  return crc;
}

uint8_t crc8(const std::string& data) {
  return crc8(reinterpret_cast<const uint8_t*>(data.data()), data.size());
}

std::string encode_v3_chunk(const std::string& payload, bool last) {
  if (payload.size() > kV3LengthMask) return {};
  const auto n = static_cast<uint8_t>(payload.size());
  uint8_t header = static_cast<uint8_t>(n & kV3LengthMask);
  if (last) header = static_cast<uint8_t>(header | kV3LastChunkBit);
  std::string packet;
  packet.reserve(payload.size() + 2);
  packet.push_back(static_cast<char>(header));
  packet.append(payload);
  packet.push_back(static_cast<char>(crc8(packet)));
  return packet;
}

bool decode_v3_chunk(const uint8_t* data, std::size_t len, V3Packet& out, V3Status& status) {
  // VerifyPacket requires len >= 3, (byte0 & 0x3F) == len - 2, and bit 7 clear.
  if (data == nullptr || len < 3) {
    status = V3Status::PayloadFormatFail;
    return false;
  }
  const uint8_t header = data[0];
  if ((header & 0x80) != 0 || (header & kV3LengthMask) != len - 2) {
    status = V3Status::PayloadFormatFail;
    return false;
  }
  const uint8_t expect = crc8(data, len - 1);
  if (data[len - 1] != expect) {
    status = V3Status::ChecksumFail;
    return false;
  }
  out.last = (header & kV3LastChunkBit) != 0;
  out.payload.assign(reinterpret_cast<const char*>(data + 1), len - 2);
  out.checksum = data[len - 1];
  status = V3Status::Success;
  return true;
}

bool self_test(std::string* detail) {
  auto fail = [&](const std::string& why) {
    if (detail) *detail = why;
    return false;
  };

  const std::string sample = "#I139.0O120.0L050B100T027.0F60.0H027.4R120C0\r";
  const StatusFrame frame = parse_v2e_status(sample, false);
  if (!frame.ok) return fail("sample frame rejected: " + std::to_string(static_cast<int>(frame.error)));

  auto need = [&](char tag, int32_t stored) {
    const auto it = frame.fields.find(tag);
    if (it == frame.fields.end() || it->second.stored != stored) {
      return fail(std::string("field ") + tag + " stored " +
                  (it == frame.fields.end() ? "missing" : std::to_string(it->second.stored)) +
                  " expected " + std::to_string(stored));
    }
    return true;
  };
  // 139.0 * 1000, 120.0 * 1000, 50 * 1000, 100 * 1000, 27.0 * 1000,
  // 60.0 * 1000, 27.4 * 1000, R: 120 minutes -> 7200 seconds, C: 0.
  if (!need('I', 139000)) return false;
  if (!need('O', 120000)) return false;
  if (!need('L', 50000)) return false;
  if (!need('B', 100000)) return false;
  if (!need('T', 27000)) return false;
  if (!need('F', 60000)) return false;
  if (!need('H', 27400)) return false;
  if (!need('R', 7200)) return false;
  if (!need('C', 0)) return false;

  if (parse_v2e_status("", false).error != Error::RespEmpty) return fail("empty should be 200");
  if (parse_v2e_status("nope\r", false).error != Error::RespFormatEssential) return fail("format should be 201");

  const std::string packet = encode_v3_chunk("D", true);
  if (packet.size() != 3 || static_cast<uint8_t>(packet[0]) != (1 | kV3LastChunkBit)) {
    return fail("v3 header");
  }
  V3Packet decoded;
  V3Status st = V3Status::Success;
  if (!decode_v3_chunk(reinterpret_cast<const uint8_t*>(packet.data()), packet.size(), decoded, st) ||
      decoded.payload != "D" || !decoded.last) {
    return fail("v3 round trip");
  }
  // CRC of a known vector: single byte 0x00 -> poly path.
  if (crc8(std::string(1, '\x00')) != 0) return fail("crc8 zero");
  const uint8_t c = crc8(std::string(1, '\x01'));
  // One byte 0x01, init 0: xor -> 0x01, then 8 shifts with poly 0xD5.
  uint8_t manual = 0x01;
  for (int bit = 0; bit < 8; ++bit) {
    const uint8_t shifted = static_cast<uint8_t>(manual << 1);
    manual = (manual & 0x80) ? static_cast<uint8_t>(shifted ^ kCrc8Poly) : shifted;
  }
  if (c != manual) return fail("crc8 mismatch");

  if (detail) *detail = "ok";
  return true;
}

}  // namespace cyberpower::protocol
