#include "cpups_format.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <optional>
#include <sstream>

const char* transport_name(cyberpower::TransportKind kind) {
  return kind == cyberpower::TransportKind::Serial ? "serial" : "hid";
}

std::string json_escape(const std::string& text) {
  std::string out;
  out.reserve(text.size());
  for (unsigned char c : text) {
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\u%04x", c);
          out += buf;
        } else {
          out.push_back(static_cast<char>(c));
        }
        break;
    }
  }
  return out;
}

namespace {

void print_opt(const char* label, const std::optional<double>& value, const char* unit) {
  std::cout << "  " << label << ": ";
  if (value) {
    std::cout << *value << (unit ? unit : "") << "\n";
  } else {
    std::cout << "n/a\n";
  }
}

// Writes `,"key":` (no leading comma for the first member of an object).
void json_key(std::ostream& out, const char* key, bool& first) {
  if (!first) out << ',';
  first = false;
  out << '"' << key << "\":";
}

void json_opt_number(std::ostream& out, const char* key, const std::optional<double>& value, bool& first) {
  json_key(out, key, first);
  if (value && std::isfinite(*value)) {
    out << *value;
  } else {
    out << "null";
  }
}

void json_opt_bool(std::ostream& out, const char* key, const std::optional<bool>& value, bool& first) {
  json_key(out, key, first);
  if (!value) {
    out << "null";
  } else {
    out << (*value ? "true" : "false");
  }
}

std::string format_opt(const std::optional<double>& value, const char* suffix) {
  if (!value) return "n/a";
  std::ostringstream out;
  out << *value;
  if (suffix) out << suffix;
  return out.str();
}

}  // namespace

void print_status_text(const cyberpower::Status& status) {
  std::cout << "  ok: " << (status.ok ? "yes" : "no");
  if (!status.ok) {
    std::cout << " (" << cyberpower::error_name(status.error);
    if (!status.message.empty()) std::cout << " " << status.message;
    std::cout << ")";
  }
  std::cout << "\n";
  if (!status.raw.empty()) {
    std::cout << "  raw: " << status.raw;
    if (status.raw.back() != '\n') std::cout << "\n";
  }
  if (status.firmware_version) std::cout << "  firmware: " << *status.firmware_version << "\n";
  print_opt("battery", status.battery_percent, "%");
  print_opt("input", status.input_voltage_v, " V");
  print_opt("output", status.output_voltage_v, " V");
  print_opt("load", status.load_percent, "%");
  print_opt("runtime", status.runtime_seconds, " s");
  print_opt("frequency", status.frequency_hz, " Hz");
  print_opt("temperature", status.temperature_c, " C");
  print_opt("battery voltage", status.battery_voltage_v, " V");
  print_opt("cycle count", status.cycle_count, nullptr);
  if (status.ac_present)       std::cout << "  ac present: "    << (*status.ac_present ? "yes" : "no") << "\n";
  if (status.charging)         std::cout << "  charging: "      << (*status.charging ? "yes" : "no") << "\n";
  if (status.discharging)      std::cout << "  discharging: "   << (*status.discharging ? "yes" : "no") << "\n";
  if (status.need_replacement) std::cout << "  needs battery: " << (*status.need_replacement ? "yes" : "no") << "\n";
  if (status.voltage_sensitivity) {
    const int s = *status.voltage_sensitivity;
    const char* label = (s == 1) ? "High" : (s == 2) ? "Medium" : (s == 3) ? "Low" : "?";
    std::cout << "  sensitivity: " << s << " (" << label << ") [Medium confidence]\n";
  }
  print_opt("shutdown delay", status.shutdown_delay_s, " s");
  print_opt("restore delay", status.restore_delay_s, " s");
}

void print_status_json_object(std::ostream& out, const cyberpower::Status& status) {
  out << '{';
  bool first = true;
  json_key(out, "ok", first);
  out << (status.ok ? "true" : "false");
  out << ",\"error\":\"" << json_escape(cyberpower::error_name(status.error)) << '"';
  out << ",\"message\":\"" << json_escape(status.message) << '"';
  out << ",\"raw\":\"" << json_escape(status.raw) << '"';
  // Core fields
  json_opt_number(out, "battery_percent",   status.battery_percent, first);
  json_opt_number(out, "input_voltage_v",   status.input_voltage_v, first);
  json_opt_number(out, "output_voltage_v",  status.output_voltage_v, first);
  json_opt_number(out, "load_percent",      status.load_percent, first);
  json_opt_number(out, "runtime_seconds",   status.runtime_seconds, first);
  json_opt_number(out, "frequency_hz",      status.frequency_hz, first);
  json_opt_number(out, "temperature_c",     status.temperature_c, first);
  json_opt_number(out, "battery_voltage_v", status.battery_voltage_v, first);
  json_opt_bool(out, "ac_present",   status.ac_present, first);
  json_opt_bool(out, "charging",     status.charging, first);
  json_opt_bool(out, "discharging",  status.discharging, first);
  // Extended HID fields
  json_key(out, "firmware_version", first);
  if (status.firmware_version) {
    out << '"' << json_escape(*status.firmware_version) << '"';
  } else {
    out << "null";
  }
  json_opt_number(out, "cycle_count",     status.cycle_count, first);
  json_opt_bool(out, "need_replacement",  status.need_replacement, first);
  // voltage_sensitivity is int, not double — emit directly
  json_key(out, "voltage_sensitivity", first);
  if (status.voltage_sensitivity) {
    out << *status.voltage_sensitivity;
  } else {
    out << "null";
  }
  json_opt_number(out, "shutdown_delay_s", status.shutdown_delay_s, first);
  json_opt_number(out, "restore_delay_s",  status.restore_delay_s, first);
  out << '}';
}

void print_device_json(std::ostream& out, const cyberpower::DeviceInfo& device,
                       const cyberpower::Status* status, const std::string* open_error) {
  out << '{';
  bool first = true;
  auto field = [&](const char* key, const std::string& value) {
    if (!first) out << ',';
    first = false;
    out << '"' << key << "\":\"" << json_escape(value) << '"';
  };
  auto field_num = [&](const char* key, long long value) {
    if (!first) out << ',';
    first = false;
    out << '"' << key << "\":" << value;
  };

  field("transport", transport_name(device.transport));
  field("path", device.path);
  field("product", device.product);
  field("serial_number", device.serial_number);
  field_num("vendor_id", device.vendor_id);
  field_num("product_id", device.product_id);
  field_num("location_id", device.location_id);

  if (open_error != nullptr) {
    field("error", *open_error);
    out << '}';
    return;
  }
  if (status == nullptr) {
    out << '}';
    return;
  }

  if (!first) out << ',';
  out << "\"status\":";
  print_status_json_object(out, *status);
  out << '}';
}

std::string format_monitor_line(const cyberpower::Status& status) {
  std::ostringstream out;
  out << "bat=" << format_opt(status.battery_percent, "%")
      << "  in=" << format_opt(status.input_voltage_v, "V")
      << "  out=" << format_opt(status.output_voltage_v, "V")
      << "  load=" << format_opt(status.load_percent, "%")
      << "  runtime=" << format_opt(status.runtime_seconds, "s");
  if (status.ac_present) out << "  ac=" << (*status.ac_present ? "yes" : "no");
  if (status.charging) out << "  chg=" << (*status.charging ? "yes" : "no");
  if (status.discharging) out << "  dis=" << (*status.discharging ? "yes" : "no");
  if (!status.ok) {
    out << "  err=" << cyberpower::error_name(status.error);
    if (!status.message.empty()) out << "(" << status.message << ")";
  }
  return out.str();
}
