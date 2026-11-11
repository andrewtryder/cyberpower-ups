#include "cyberpower/protocol.hpp"

#include <cmath>
#include <sstream>

namespace cyberpower::protocol {

bool battery_usage_67_is_legacy_full_capacity(int logical_min, int logical_max) {
  return logical_min >= 0 && logical_max > 200;
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
