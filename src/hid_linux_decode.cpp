#include "internal/hid_linux_decode.hpp"

namespace cyberpower::platform {
namespace {

uint16_t read_le16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

}  // namespace

bool is_verified_hid_profile(uint16_t vid, uint16_t pid) {
  return vid == protocol::kVendorId && pid == protocol::kPid0601;
}

bool validate_feature_report(const uint8_t* buf, ssize_t bytes_read,
                             uint8_t expected_id, std::size_t min_len) {
  if (buf == nullptr || min_len < 1) return false;
  if (bytes_read < static_cast<ssize_t>(min_len)) return false;
  if (buf[0] != expected_id) return false;
  return true;
}

bool decode_cp1500_report_0b(const uint8_t* buf, std::size_t len, Readings& r, Status& s) {
  if (!validate_feature_report(buf, static_cast<ssize_t>(len), 0x0B, 2)) return false;
  const uint8_t flags = buf[1];

  HidElement ac_elem;
  ac_elem.page = protocol::kPageBattery;
  ac_elem.usage = 0xD0;
  map_element(ac_elem, (flags & 0x01) ? 1.0 : 0.0, r, s);

  HidElement chg_elem;
  chg_elem.page = protocol::kPageBattery;
  chg_elem.usage = 0x44;
  map_element(chg_elem, (flags & 0x02) ? 1.0 : 0.0, r, s);

  HidElement dis_elem;
  dis_elem.page = protocol::kPageBattery;
  dis_elem.usage = 0x45;
  map_element(dis_elem, (flags & 0x04) ? 1.0 : 0.0, r, s);

  return true;
}

bool decode_cp1500_report_08(const uint8_t* buf, std::size_t len, Readings& r, Status& s) {
  if (!validate_feature_report(buf, static_cast<ssize_t>(len), 0x08, 4)) return false;

  // buf[1]: Remaining capacity percent (0-100)
  if (buf[1] <= 100) {
    HidElement rem_elem;
    rem_elem.page = protocol::kPageBattery;
    rem_elem.usage = protocol::kUsageDischarging;  // 0x66 legacy / percent
    rem_elem.logical_max = 100;
    map_element(rem_elem, static_cast<double>(buf[1]), r, s);
  }

  // buf[2..3]: Runtime in seconds (LE16)
  const uint16_t rt_val = read_le16(buf + 2);
  HidElement rt_elem;
  rt_elem.page = protocol::kPageBattery;
  rt_elem.usage = protocol::kUsageCharging;  // 0x68 legacy runtime
  rt_elem.logical_max = 65535;
  map_element(rt_elem, static_cast<double>(rt_val), r, s);

  return true;
}

bool decode_cp1500_report_0f(const uint8_t* buf, std::size_t len, Readings& r, Status& s) {
  if (!validate_feature_report(buf, static_cast<ssize_t>(len), 0x0F, 3)) return false;
  HidElement in_elem;
  in_elem.page = protocol::kPagePowerDevice;
  in_elem.usage = protocol::kUsageVoltage;
  in_elem.ancestors = {{protocol::kPagePowerDevice, 0x1A}};
  const uint16_t val = read_le16(buf + 1);
  map_element(in_elem, static_cast<double>(val), r, s);
  return true;
}

bool decode_cp1500_report_12(const uint8_t* buf, std::size_t len, Readings& r, Status& s) {
  if (!validate_feature_report(buf, static_cast<ssize_t>(len), 0x12, 3)) return false;
  HidElement out_elem;
  out_elem.page = protocol::kPagePowerDevice;
  out_elem.usage = protocol::kUsageVoltage;
  out_elem.ancestors = {{protocol::kPagePowerDevice, 0x1C}};
  const uint16_t val = read_le16(buf + 1);
  map_element(out_elem, static_cast<double>(val), r, s);
  return true;
}

bool decode_cp1500_report_13(const uint8_t* buf, std::size_t len, Readings& r, Status& s) {
  if (!validate_feature_report(buf, static_cast<ssize_t>(len), 0x13, 2)) return false;
  if (buf[1] <= 100) {
    HidElement load_elem;
    load_elem.page = protocol::kPagePowerDevice;
    load_elem.usage = 0x35;
    map_element(load_elem, static_cast<double>(buf[1]), r, s);
  }
  return true;
}

bool decode_cp1500_report_0a(const uint8_t* buf, std::size_t len, Readings& r, Status& s) {
  if (!validate_feature_report(buf, static_cast<ssize_t>(len), 0x0A, 3)) return false;
  HidElement bat_elem;
  bat_elem.page = protocol::kPagePowerDevice;
  bat_elem.usage = protocol::kUsageVoltage;
  bat_elem.ancestors = {{protocol::kPagePowerDevice, 0x12}};
  const uint16_t val = read_le16(buf + 1);
  map_element(bat_elem, static_cast<double>(val) / 10.0, r, s);
  return true;
}

bool decode_cp1500_report_07(const uint8_t* buf, std::size_t len, Readings& r, Status& s) {
  // CP1500PFCLCDa Report 0x07 is 7 bytes (1 ID + 6 data bytes):
  // [1]=DesignCapacity(0x83), [2]=CapacityMode(0x8D), [3]=ManufacturerDate(0x8E),
  // [4]=FullChargeCapacity(0x8C), [5]=RemainingCapacityLimit(0x29), [6]=FullChargeCapacity(0x67).
  // Note: None of these fields correspond to NeedReplacement (0x2B).
  // Therefore Status::need_replacement is deliberately left unset.
  if (!validate_feature_report(buf, static_cast<ssize_t>(len), 0x07, 7)) return false;

  // buf[3]: CycleCount / Date (0x8C / 0x8E depending on layout)
  if (buf[3] > 0) {
    HidElement cycle_elem;
    cycle_elem.page = protocol::kPageBattery;
    cycle_elem.usage = protocol::kUsageCycleCount;
    map_element(cycle_elem, static_cast<double>(buf[3]), r, s);
  }

  return true;
}

bool decode_cp1500_report_02(const uint8_t* buf, std::size_t len, Readings& r, Status& s) {
  if (!validate_feature_report(buf, static_cast<ssize_t>(len), 0x02, 2)) return false;
  // Sensitivity levels: 1 = High, 2 = Medium, 3 = Low
  if (buf[1] >= 1 && buf[1] <= 3) {
    HidElement sens_elem;
    sens_elem.page = protocol::kPageVendorFf86;
    sens_elem.usage = protocol::kUsageVendorSensitivityRead;
    map_element(sens_elem, static_cast<double>(buf[1]), r, s);
    return true;
  }
  return false;
}

bool is_valid_cp1500_alarm_control(int value) {
  // Report 0x0C: 1 = Disable, 2 = Enable, 3 = Mute
  return value >= 1 && value <= 3;
}

bool is_valid_cp1500_test_mode(int value) {
  // Report 0x14: 1 = Quick Test, 2 = Deep Test (Calibrate), 3 = Cancel Test
  return value >= 1 && value <= 3;
}

}  // namespace cyberpower::platform

