#include "internal/c_api_internal.hpp"

#include <cstdlib>
#include <cstring>
#include <optional>

namespace cyberpower::capi {

namespace {

void set_opt(int& has, double& dest, const std::optional<double>& value) {
  if (value) {
    has = 1;
    dest = *value;
  }
}

}  // namespace

char* dup_cstr(const std::string& text) {
  char* out = static_cast<char*>(std::malloc(text.size() + 1));
  if (out == nullptr) return nullptr;
  std::memcpy(out, text.c_str(), text.size() + 1);
  return out;
}

void fill_status(cp_status* out, const cyberpower::Status& status) {
  std::memset(out, 0, sizeof *out);
  out->ok = status.ok ? 1 : 0;
  out->error = static_cast<int>(status.error);
  out->message = dup_cstr(status.message);
  out->transport = status.transport == cyberpower::TransportKind::Serial ? 1 : 0;
  out->raw = dup_cstr(status.raw);
  set_opt(out->has_battery_percent, out->battery_percent, status.battery_percent);
  set_opt(out->has_input_voltage_v, out->input_voltage_v, status.input_voltage_v);
  set_opt(out->has_output_voltage_v, out->output_voltage_v, status.output_voltage_v);
  set_opt(out->has_load_percent, out->load_percent, status.load_percent);
  set_opt(out->has_runtime_seconds, out->runtime_seconds, status.runtime_seconds);
  set_opt(out->has_frequency_hz, out->frequency_hz, status.frequency_hz);
  set_opt(out->has_temperature_c, out->temperature_c, status.temperature_c);
  set_opt(out->has_battery_voltage_v, out->battery_voltage_v, status.battery_voltage_v);
  if (status.ac_present) {
    out->has_ac_present = 1;
    out->ac_present = *status.ac_present ? 1 : 0;
  }
  if (status.charging) {
    out->has_charging = 1;
    out->charging = *status.charging ? 1 : 0;
  }
  if (status.discharging) {
    out->has_discharging = 1;
    out->discharging = *status.discharging ? 1 : 0;
  }
  if (status.firmware_version) {
    out->firmware_version = dup_cstr(*status.firmware_version);
  }
  set_opt(out->has_cycle_count, out->cycle_count, status.cycle_count);
  if (status.need_replacement) {
    out->has_need_replacement = 1;
    out->need_replacement = *status.need_replacement ? 1 : 0;
  }
  if (status.voltage_sensitivity) {
    out->has_voltage_sensitivity = 1;
    out->voltage_sensitivity = *status.voltage_sensitivity;
  }
  set_opt(out->has_shutdown_delay_s, out->shutdown_delay_s, status.shutdown_delay_s);
  set_opt(out->has_restore_delay_s, out->restore_delay_s, status.restore_delay_s);
}

}  // namespace cyberpower::capi
