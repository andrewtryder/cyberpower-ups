#include "cyberpower/protocol_v3.hpp"

namespace cyberpower::protocol {

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

}  // namespace cyberpower::protocol
