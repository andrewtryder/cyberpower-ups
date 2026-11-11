#include "internal/hid_mapping.hpp"

#include "cyberpower/protocol.hpp"

#include <sstream>

namespace cyberpower::platform {
namespace {

bool is_flag(double value) { return value == 0.0 || value == 1.0; }

// Folds one element into `r` (or directly into `status` for boolean flags).
// Returns a short label describing the mapping for --dump-raw, or nullptr when
// the element was ignored.
//
// CyberPower frequently maps status to Feature elements, so core fields are
// deliberately not restricted to Input elements.
const char* map_power_device(const HidElement& element, uint32_t usage, double value, Readings& r,
                             Status& status) {
  switch (usage) {
    case 0x30:
      if (element.ancestor_is(protocol::kPagePowerDevice, 0x1A)) {
        r.input_v = value; return "input_v";
      }
      if (element.ancestor_is(protocol::kPagePowerDevice, 0x1C)) {
        r.output_v = value; return "output_v";
      }
      if (element.ancestor_is(protocol::kPagePowerDevice, 0x12)) {
        r.battery_v = value; return "battery_v";
      }
      if (r.loose_voltage == 0) {
        r.input_v = value; ++r.loose_voltage; return "input_v(loose)";
      }
      if (r.loose_voltage == 1) {
        r.output_v = value; ++r.loose_voltage; return "output_v(loose)";
      }
      return nullptr;
    case 0x35: r.load = value; return "load(35_legacy)";
    case 0x32: r.freq = value; return "freq";
    case 0x36: r.temp = value; return "temp(older)";
    case protocol::kUsagePercentLoad:
      if (!r.load) { r.load = value; return "load(65_new)"; }
      return nullptr;
    case protocol::kUsageACPresent:
      if (is_flag(value)) { status.ac_present = value != 0.0; return "ac_present"; }
      return nullptr;
    default: return nullptr;
  }
}

// Battery System page (0x85): RE-confirmed primary usages.
const char* map_battery(const HidElement& element, uint32_t usage, double value, Readings& r,
                        Status& status) {
  switch (usage) {
    case protocol::kUsageRemainingCapacity:
      // 0x85/0x2C is new. Only use if legacy 0x66 didn't map yet.
      if (!r.remain) { r.remain = value; return "remain(2C_new)"; }
      return nullptr;
    case protocol::kUsageRunTimeToEmpty:
      // 0x85/0x8B is new.
      if (!r.runtime) { r.runtime = value; return "runtime(8B_new)"; }
      return nullptr;
    case protocol::kUsageFullChargeCapacity:
      r.full = value; return "full_cap(8D)";
    case protocol::kUsageTemperatureBat: {
      // 0x67 is Temperature in the modern RE-confirmed table, but older
      // firmware used the same usage for FullChargeCapacity. Disambiguate
      // from descriptor metadata rather than the live value: a capacity
      // element has a range beyond plausible Celsius values. This keeps the
      // later legacy mapping reachable without reclassifying all 0x67 data.
      if (protocol::battery_usage_67_is_legacy_full_capacity(element.logical_min, element.logical_max)) {
        if (!r.full) { r.full = value; return "full_cap(67_legacy)"; }
      } else if (!r.temp && value < 99.0) {
        r.temp = value; return "temp(67_new)";
      }
      return nullptr;
    }
    case protocol::kUsageCycleCount:
      r.cycle_count = value; return "cycle_count(8C)";
    case protocol::kUsageNeedReplacement:
      if (is_flag(value)) { r.need_replacement = value != 0.0; return "need_repl(29)"; }
      return nullptr;
    case protocol::kUsageCharging: {  // 0x68
      const bool legacy_numeric = element.logical_max > 1;
      if (legacy_numeric) { r.runtime = value; return "runtime(68_legacy)"; }
      if (is_flag(value)) { status.charging = value != 0.0; return "charging(68)"; }
      return nullptr;
    }
    case protocol::kUsageDischarging: {  // 0x66
      const bool legacy_numeric = element.logical_max > 1;
      if (legacy_numeric) { r.remain = value; return "remain(66_legacy)"; }
      if (is_flag(value)) { status.discharging = value != 0.0; return "discharging(66)"; }
      return nullptr;
    }
    default: break;
  }
  return nullptr;
}

// Alternative boolean usages that exist on both the Battery page and the
// vendor 0xFF01 page.
const char* map_alt_flags(uint32_t page, uint32_t usage, double value, Status& status) {
  if (!is_flag(value)) return nullptr;
  const bool battery = page == protocol::kPageBattery;
  const bool vendor = page == protocol::kPageVendorFf01;
  if ((battery && usage == 0xD0) || (vendor && usage == 0xD0)) {
    status.ac_present = value != 0.0; return "ac_present(alt)";
  }
  if ((battery && (usage == 0x44 || usage == 0xD1)) || (vendor && usage == 0xD1)) {
    status.charging = value != 0.0; return "charging(alt)";
  }
  if ((battery && (usage == 0x45 || usage == 0xD2)) || (vendor && usage == 0xD2)) {
    status.discharging = value != 0.0; return "discharging(alt)";
  }
  return nullptr;
}

// Vendor page 0xFF86: config reads.
const char* map_vendor_ff86(uint32_t usage, double value, Readings& r) {
  if (usage == protocol::kUsageVendorSensitivityRead) {
    r.sensitivity = value; return "sensitivity(FF86)";
  }
  if (usage == protocol::kUsageVendorShutdownDelay) {
    r.shutdown_delay = value; return "shutdown_delay(FF86)";
  }
  if (usage == protocol::kUsageVendorRestoreDelay) {
    r.restore_delay = value; return "restore_delay(FF86)";
  }
  return nullptr;
}

}  // namespace

const char* element_type_label(HidElementType type) {
  switch (type) {
    case HidElementType::Input: return "In";
    case HidElementType::Feature: return "Feat";
    case HidElementType::Output: return "Out";
    case HidElementType::Other: return "Other";
  }
  return "Other";
}

const char* map_element(const HidElement& element, double value, Readings& r, Status& status) {
  const uint32_t page = element.page;
  const uint32_t usage = element.usage;
  if (page == protocol::kPagePowerDevice) return map_power_device(element, usage, value, r, status);
  if (page == protocol::kPageVendorFf86) return map_vendor_ff86(usage, value, r);
  if (page == protocol::kPageBattery) {
    if (const char* label = map_battery(element, usage, value, r, status)) return label;
  }
  return map_alt_flags(page, usage, value, status);
}

// Converts the gathered readings into the public Status fields.
void apply_readings(const Readings& r, Status& status) {
  status.load_percent = r.load;
  status.frequency_hz = r.freq;
  status.temperature_c = r.temp;
  status.input_voltage_v = r.input_v;
  status.output_voltage_v = r.output_v;
  status.battery_voltage_v = r.battery_v;
  status.runtime_seconds = r.runtime;
  if (r.remain) {
    // RemainingCapacity is a percent on most of these UPSes. If the device
    // also exposes a larger full-charge capacity, convert.
    // Medium: the HID unit is not printed in the driver.
    if (r.full && *r.full > 0.0 && (*r.remain > 100.0 || *r.full > 100.0)) {
      status.battery_percent = (*r.remain / *r.full) * 100.0;
    } else {
      status.battery_percent = r.remain;
    }
  }
  if (r.cycle_count) status.cycle_count = r.cycle_count;
  if (r.need_replacement) status.need_replacement = r.need_replacement;
  if (r.sensitivity) status.voltage_sensitivity = static_cast<int>(*r.sensitivity);
  if (r.shutdown_delay) status.shutdown_delay_s = r.shutdown_delay;
  if (r.restore_delay) status.restore_delay_s = r.restore_delay;
}

void append_summary(std::ostringstream& out, const char* label, const std::optional<double>& v) {
  out << label;
  if (v) out << *v; else out << "n/a";
}

std::string summarize(const Readings& r, const Status& status) {
  std::ostringstream out;
  append_summary(out, "hid usages bat=", r.remain);
  append_summary(out, " load=", r.load);
  append_summary(out, " in=", r.input_v);
  append_summary(out, " out=", r.output_v);
  append_summary(out, " runtime=", r.runtime);
  out << " ac=";
  if (status.ac_present) out << (*status.ac_present ? 1 : 0); else out << "n/a";
  out << " sens=";
  if (r.sensitivity) out << static_cast<int>(*r.sensitivity); else out << "n/a";
  append_summary(out, " shutd=", r.shutdown_delay);
  append_summary(out, " rest=", r.restore_delay);
  return out.str();
}

}  // namespace cyberpower::platform
