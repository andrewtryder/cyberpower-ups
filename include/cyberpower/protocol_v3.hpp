#pragma once

// v3 binary packet framing (handshake, chunk header, CRC-8).
// Provenance tags (High / Medium / Low / Speculative) are defined in protocol.hpp.

#include "cyberpower/errors.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace cyberpower::protocol {

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

}  // namespace cyberpower::protocol
