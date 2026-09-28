#include "cyberpower/protocol.hpp"
#include "cyberpower/ups.hpp"

#include <cmath>
#include <cstdio>
#include <iostream>
#include <string>
#include <vector>

namespace {

void usage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " [--self-test] [--json]\n"
            << "  (default)     list CyberPower UPS devices and print status\n"
            << "  --self-test   run protocol parser checks without hardware\n"
            << "  --json        print status as JSON\n";
}

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

void print_opt(const char* label, const std::optional<double>& value, const char* unit) {
  std::cout << "  " << label << ": ";
  if (value) {
    std::cout << *value << (unit ? unit : "") << "\n";
  } else {
    std::cout << "n/a\n";
  }
}

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
  print_opt("battery", status.battery_percent, "%");
  print_opt("input", status.input_voltage_v, " V");
  print_opt("output", status.output_voltage_v, " V");
  print_opt("load", status.load_percent, "%");
  print_opt("runtime", status.runtime_seconds, " s");
  print_opt("frequency", status.frequency_hz, " Hz");
  print_opt("temperature", status.temperature_c, " C");
  print_opt("battery voltage", status.battery_voltage_v, " V");
  if (status.ac_present) std::cout << "  ac present: " << (*status.ac_present ? "yes" : "no") << "\n";
  if (status.charging) std::cout << "  charging: " << (*status.charging ? "yes" : "no") << "\n";
  if (status.discharging) std::cout << "  discharging: " << (*status.discharging ? "yes" : "no") << "\n";
}

void json_opt_number(std::ostream& out, const char* key, const std::optional<double>& value, bool& first) {
  if (!first) out << ',';
  first = false;
  out << '"' << key << "\":";
  if (!value) {
    out << "null";
    return;
  }
  if (!std::isfinite(*value)) {
    out << "null";
    return;
  }
  out << *value;
}

void json_opt_bool(std::ostream& out, const char* key, const std::optional<bool>& value, bool& first) {
  if (!first) out << ',';
  first = false;
  out << '"' << key << "\":";
  if (!value) {
    out << "null";
  } else {
    out << (*value ? "true" : "false");
  }
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
  out << "\"status\":{";
  bool sfirst = true;
  out << "\"ok\":" << (status->ok ? "true" : "false");
  sfirst = false;
  out << ",\"error\":\"" << json_escape(cyberpower::error_name(status->error)) << '"';
  out << ",\"message\":\"" << json_escape(status->message) << '"';
  out << ",\"raw\":\"" << json_escape(status->raw) << '"';
  json_opt_number(out, "battery_percent", status->battery_percent, sfirst);
  json_opt_number(out, "input_voltage_v", status->input_voltage_v, sfirst);
  json_opt_number(out, "output_voltage_v", status->output_voltage_v, sfirst);
  json_opt_number(out, "load_percent", status->load_percent, sfirst);
  json_opt_number(out, "runtime_seconds", status->runtime_seconds, sfirst);
  json_opt_number(out, "frequency_hz", status->frequency_hz, sfirst);
  json_opt_number(out, "temperature_c", status->temperature_c, sfirst);
  json_opt_number(out, "battery_voltage_v", status->battery_voltage_v, sfirst);
  json_opt_bool(out, "ac_present", status->ac_present, sfirst);
  json_opt_bool(out, "charging", status->charging, sfirst);
  json_opt_bool(out, "discharging", status->discharging, sfirst);
  out << "}}";
}

}  // namespace

int main(int argc, char** argv) {
  bool self_test_only = false;
  bool json = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--self-test") {
      self_test_only = true;
    } else if (arg == "--json") {
      json = true;
    } else if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      return 0;
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  std::string detail;
  if (!cyberpower::protocol::self_test(&detail)) {
    std::cerr << "protocol self-test failed: " << detail << "\n";
    return 1;
  }
  if (self_test_only) {
    if (json) {
      std::cout << "{\"self_test\":\"ok\"}\n";
    } else {
      std::cout << "protocol self-test passed\n";
    }
    return 0;
  }

  const std::vector<cyberpower::DeviceInfo> devices = cyberpower::list_devices();
  if (json) {
    std::cout << "{\"devices\":[";
    for (std::size_t i = 0; i < devices.size(); ++i) {
      if (i != 0) std::cout << ',';
      std::string error;
      std::optional<cyberpower::Ups> ups = cyberpower::open_device(devices[i], &error);
      if (!ups) {
        print_device_json(std::cout, devices[i], nullptr, &error);
        continue;
      }
      const cyberpower::Status status = ups->read_status();
      print_device_json(std::cout, devices[i], &status, nullptr);
    }
    std::cout << "]}\n";
    return 0;
  }

  std::cout << "devices: " << devices.size() << "\n";
  if (devices.empty()) {
    std::cout << "No CyberPower HID device (vendor 0x0764) or matching serial node was found.\n";
    return 0;
  }

  for (const cyberpower::DeviceInfo& device : devices) {
    std::cout << transport_name(device.transport) << " " << device.path;
    if (!device.product.empty()) std::cout << "  " << device.product;
    if (device.vendor_id != 0) {
      std::cout << "  vid=" << device.vendor_id << " pid=" << device.product_id;
    }
    std::cout << "\n";

    std::string error;
    std::optional<cyberpower::Ups> ups = cyberpower::open_device(device, &error);
    if (!ups) {
      std::cout << "  open failed: " << error << "\n";
      continue;
    }
    print_status_text(ups->read_status());
  }
  return 0;
}
