#pragma once

#include "internal/hid_mapping.hpp"
#include "cyberpower/protocol.hpp"
#include "cyberpower/ups.hpp"

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

namespace cyberpower::platform {

// Only verified CyberPower UPS models are supported for fixed Feature report layouts.
// Verified model: CP1500PFCLCDa (VID 0x0764, PID 0x0601).
bool is_verified_hid_profile(uint16_t vid, uint16_t pid);

// Validates that a Feature report response has sufficient length and the expected report ID.
bool validate_feature_report(const uint8_t* buf, ssize_t bytes_read,
                             uint8_t expected_id, std::size_t min_len);

// Decoding helpers for CP1500PFCLCDa (PID 0x0601).
// Each returns true if the report was valid and successfully mapped to readings/status.
bool decode_cp1500_report_0b(const uint8_t* buf, std::size_t len, Readings& r, Status& s);
bool decode_cp1500_report_08(const uint8_t* buf, std::size_t len, Readings& r, Status& s);
bool decode_cp1500_report_0f(const uint8_t* buf, std::size_t len, Readings& r, Status& s);
bool decode_cp1500_report_12(const uint8_t* buf, std::size_t len, Readings& r, Status& s);
bool decode_cp1500_report_13(const uint8_t* buf, std::size_t len, Readings& r, Status& s);
bool decode_cp1500_report_0a(const uint8_t* buf, std::size_t len, Readings& r, Status& s);
bool decode_cp1500_report_07(const uint8_t* buf, std::size_t len, Readings& r, Status& s);
bool decode_cp1500_report_02(const uint8_t* buf, std::size_t len, Readings& r, Status& s);

// Range validation for control writes on CP1500PFCLCDa (PID 0x0601).
bool is_valid_cp1500_alarm_control(int value);
bool is_valid_cp1500_test_mode(int value);

}  // namespace cyberpower::platform

