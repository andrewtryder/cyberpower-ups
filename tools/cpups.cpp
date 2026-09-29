#include "cyberpower/protocol.hpp"
#include "cyberpower/ups.hpp"
#include "internal/parse_integer.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) { g_stop.store(true, std::memory_order_relaxed); }

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

void usage(const char* argv0) {
  std::cerr << "Usage: " << argv0 << " [options]\n"
            << "  (default)            list CyberPower UPS devices and print status\n"
            << "  --self-test          run protocol parser checks without hardware\n"
            << "  --json               print status/command result as JSON\n"
            << "  --monitor            poll the first UPS and print live status\n"
            << "  --interval MS        monitor poll interval in milliseconds (default: 2000)\n"
            << "  --every-poll         with --monitor, print every sample (default: on change)\n"
            << "  --dump-raw           print serial TX/RX or HID report hex to stderr while reading\n"
            << "\n"
            << "  Device commands (first UPS):\n"
            << "  --test, --self-test-device   quick self-test (HID+serial)\n"
            << "  --cancel-test                cancel self-test (HID+serial)\n"
            << "  --buzzer, --beep             toggle audible alarm (HID+serial)\n"
            << "  --mute / --enable-alarm / --disable-alarm   HID Feature alarm\n"
            << "  --calibrate                  serial TL, or HID deep Test(2)\n"
            << "  --rating                     query rating (serial only)\n"
            << "  --cancel-schedule            cancel schedule (serial only)\n"
            << "  --indicator-test             LED test (serial only)\n"
            << "  --buzzer-test                buzzer test (serial only)\n"
            << "\n"
            << "  HID-only config writes:\n"
            << "  --set-sensitivity N          set voltage sensitivity (1=High, 2=Medium, 3=Low)\n"
            << "                               (Medium confidence on encoding; see PROTOCOL.md)\n"
            << "  --set-shutdown-delay N       set shutdown delay in seconds (HID 0xFF86/0x16)\n"
            << "  --set-restore-delay N        set restore/startup delay in seconds (HID 0xFF86/0x52)\n";
}

enum class DeviceAction {
  None,
  SelfTest,
  CancelTest,
  ToggleBuzzer,
  MuteAlarm,
  EnableAlarm,
  DisableAlarm,
  Rating,
  CancelSchedule,
  Calibrate,
  IndicatorTest,
  BuzzerTest,
  SetSensitivity,
  SetShutdownDelay,
  SetRestoreDelay,
};

int g_action_param = 0;  // numeric parameter for Set* actions

const char* device_action_name(DeviceAction action) {
  switch (action) {
    case DeviceAction::SelfTest: return "self_test";
    case DeviceAction::CancelTest: return "cancel_test";
    case DeviceAction::ToggleBuzzer: return "toggle_buzzer";
    case DeviceAction::MuteAlarm: return "mute_alarm";
    case DeviceAction::EnableAlarm: return "enable_alarm";
    case DeviceAction::DisableAlarm: return "disable_alarm";
    case DeviceAction::Rating: return "rating";
    case DeviceAction::CancelSchedule: return "cancel_schedule";
    case DeviceAction::Calibrate: return "calibrate";
    case DeviceAction::IndicatorTest: return "indicator_test";
    case DeviceAction::BuzzerTest: return "buzzer_test";
    case DeviceAction::SetSensitivity: return "set_sensitivity";
    case DeviceAction::SetShutdownDelay: return "set_shutdown_delay";
    case DeviceAction::SetRestoreDelay: return "set_restore_delay";
    case DeviceAction::None: return "none";
  }
  return "none";
}

bool set_device_action(DeviceAction& current, DeviceAction next) {
  if (current != DeviceAction::None && current != next) {
    std::cerr << "only one device command may be specified at a time\n";
    return false;
  }
  current = next;
  return true;
}

int run_device_action(cyberpower::Ups& ups, DeviceAction action, bool json) {
  cyberpower::Error error = cyberpower::Error::Ok;
  std::string rating;

  switch (action) {
    case DeviceAction::SelfTest: error = ups.self_test(); break;
    case DeviceAction::CancelTest: error = ups.cancel_test(); break;
    case DeviceAction::ToggleBuzzer: error = ups.toggle_buzzer(); break;
    case DeviceAction::MuteAlarm: error = ups.mute_alarm(); break;
    case DeviceAction::EnableAlarm: error = ups.enable_alarm(); break;
    case DeviceAction::DisableAlarm: error = ups.disable_alarm(); break;
    case DeviceAction::Rating: error = ups.read_rating(rating); break;
    case DeviceAction::CancelSchedule: error = ups.cancel_schedule(); break;
    case DeviceAction::Calibrate: error = ups.calibrate(); break;
    case DeviceAction::IndicatorTest: error = ups.indicator_test(); break;
    case DeviceAction::BuzzerTest: error = ups.buzzer_test(); break;
    case DeviceAction::SetSensitivity: error = ups.set_voltage_sensitivity(g_action_param); break;
    case DeviceAction::SetShutdownDelay: error = ups.set_shutdown_delay(g_action_param); break;
    case DeviceAction::SetRestoreDelay: error = ups.set_restore_delay(g_action_param); break;
    case DeviceAction::None: return 0;
  }

  const bool ok = error == cyberpower::Error::Ok;
  if (json) {
    std::cout << "{\"command\":\"" << device_action_name(action) << "\""
              << ",\"ok\":" << (ok ? "true" : "false")
              << ",\"error\":\"" << json_escape(cyberpower::error_name(error)) << "\""
              << ",\"path\":\"" << json_escape(ups.info().path) << "\""
              << ",\"transport\":\"" << transport_name(ups.info().transport) << "\""
              << ",\"response\":\"" << json_escape(rating) << "\"}\n";
  } else {
    std::cout << device_action_name(action) << " on "
              << transport_name(ups.info().transport) << " " << ups.info().path
              << ": " << (ok ? "ok" : cyberpower::error_name(error)) << "\n";
    if (action == DeviceAction::Rating) {
      std::cout << "rating: ";
      if (rating.empty()) {
        std::cout << "(empty)\n";
      } else {
        std::cout << rating;
        if (rating.back() != '\n') std::cout << "\n";
      }
    }
  }
  return ok ? 0 : 1;
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

void print_status_json_object(std::ostream& out, const cyberpower::Status& status) {
  out << '{';
  bool first = true;
  out << "\"ok\":" << (status.ok ? "true" : "false");
  first = false;
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
  if (!first) out << ',';
  first = false;
  out << "\"firmware_version\":";
  if (status.firmware_version) {
    out << '"' << json_escape(*status.firmware_version) << '"';
  } else {
    out << "null";
  }
  json_opt_number(out, "cycle_count",     status.cycle_count, first);
  json_opt_bool(out, "need_replacement",  status.need_replacement, first);
  // voltage_sensitivity is int, not double — emit directly
  if (!first) out << ',';
  first = false;
  out << "\"voltage_sensitivity\":";
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

std::string format_opt(const std::optional<double>& value, const char* suffix) {
  if (!value) return "n/a";
  std::ostringstream out;
  out << *value;
  if (suffix) out << suffix;
  return out.str();
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

int run_monitor(cyberpower::Ups& ups, int interval_ms, bool only_on_change, bool json) {
  std::signal(SIGINT, on_signal);
#ifdef SIGTERM
  std::signal(SIGTERM, on_signal);
#endif

  const auto& info = ups.info();
  if (!json) {
    std::cerr << "monitoring " << transport_name(info.transport) << " " << info.path
              << " every " << interval_ms << " ms"
              << (only_on_change ? " (on change)" : " (every poll)")
              << "; Ctrl-C to stop\n";
  }

  cyberpower::MonitorOptions options;
  options.interval = std::chrono::milliseconds(interval_ms);
  options.only_on_change = only_on_change;

  bool used_carriage_return = false;
  ups.monitor(
      options,
      [&](const cyberpower::Status& status) {
        if (json) {
          print_status_json_object(std::cout, status);
          std::cout << "\n" << std::flush;
          return;
        }
        const std::string line = format_monitor_line(status);
        if (only_on_change) {
          std::cout << line << "\n" << std::flush;
        } else {
          std::cout << "\r" << line << "          " << std::flush;
          used_carriage_return = true;
        }
      },
      g_stop);

  if (used_carriage_return) std::cout << "\n";
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  bool self_test_only = false;
  bool json = false;
  bool monitor = false;
  bool every_poll = false;
  bool dump_raw = false;
  int interval_ms = 2000;
  DeviceAction device_action = DeviceAction::None;

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--self-test") {
      self_test_only = true;
    } else if (arg == "--json") {
      json = true;
    } else if (arg == "--monitor") {
      monitor = true;
    } else if (arg == "--every-poll") {
      every_poll = true;
    } else if (arg == "--dump-raw") {
      dump_raw = true;
    } else if (arg == "--interval") {
      if (i + 1 >= argc) {
        usage(argv[0]);
        return 2;
      }
      if (!cyberpower::internal::parse_int_strict(argv[++i], interval_ms) || interval_ms <= 0) {
        std::cerr << "invalid --interval; expected positive milliseconds\n";
        return 2;
      }
    } else if (arg == "--test" || arg == "--self-test-device") {
      if (!set_device_action(device_action, DeviceAction::SelfTest)) return 2;
    } else if (arg == "--cancel-test") {
      if (!set_device_action(device_action, DeviceAction::CancelTest)) return 2;
    } else if (arg == "--buzzer" || arg == "--beep") {
      if (!set_device_action(device_action, DeviceAction::ToggleBuzzer)) return 2;
    } else if (arg == "--mute") {
      if (!set_device_action(device_action, DeviceAction::MuteAlarm)) return 2;
    } else if (arg == "--enable-alarm") {
      if (!set_device_action(device_action, DeviceAction::EnableAlarm)) return 2;
    } else if (arg == "--disable-alarm") {
      if (!set_device_action(device_action, DeviceAction::DisableAlarm)) return 2;
    } else if (arg == "--rating") {
      if (!set_device_action(device_action, DeviceAction::Rating)) return 2;
    } else if (arg == "--cancel-schedule") {
      if (!set_device_action(device_action, DeviceAction::CancelSchedule)) return 2;
    } else if (arg == "--calibrate") {
      if (!set_device_action(device_action, DeviceAction::Calibrate)) return 2;
    } else if (arg == "--indicator-test") {
      if (!set_device_action(device_action, DeviceAction::IndicatorTest)) return 2;
    } else if (arg == "--buzzer-test") {
      if (!set_device_action(device_action, DeviceAction::BuzzerTest)) return 2;
    } else if (arg == "--set-sensitivity") {
      if (i + 1 >= argc) { usage(argv[0]); return 2; }
      if (!cyberpower::internal::parse_int_strict(argv[++i], g_action_param) ||
          g_action_param < 1 || g_action_param > 3) {
        std::cerr << "--set-sensitivity: value must be 1 (High), 2 (Medium), or 3 (Low)\n";
        return 2;
      }
      if (!set_device_action(device_action, DeviceAction::SetSensitivity)) return 2;
    } else if (arg == "--set-shutdown-delay") {
      if (i + 1 >= argc) { usage(argv[0]); return 2; }
      if (!cyberpower::internal::parse_int_strict(argv[++i], g_action_param) || g_action_param < 0) {
        std::cerr << "--set-shutdown-delay: value must be non-negative seconds\n";
        return 2;
      }
      if (!set_device_action(device_action, DeviceAction::SetShutdownDelay)) return 2;
    } else if (arg == "--set-restore-delay") {
      if (i + 1 >= argc) { usage(argv[0]); return 2; }
      if (!cyberpower::internal::parse_int_strict(argv[++i], g_action_param) || g_action_param < 0) {
        std::cerr << "--set-restore-delay: value must be non-negative seconds\n";
        return 2;
      }
      if (!set_device_action(device_action, DeviceAction::SetRestoreDelay)) return 2;
    } else if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      return 0;
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  if (monitor && device_action != DeviceAction::None) {
    std::cerr << "--monitor cannot be combined with a device command\n";
    return 2;
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

  if (dump_raw) {
    cyberpower::set_raw_dump_sink(&std::cerr);
  }

  const std::vector<cyberpower::DeviceInfo> devices = cyberpower::list_devices();

  if (monitor || device_action != DeviceAction::None) {
    if (devices.empty()) {
      std::cerr << "No CyberPower HID device (vendor 0x0764) or matching serial node was found.\n";
      return 1;
    }
    std::string error;
    std::optional<cyberpower::Ups> ups = cyberpower::open_device(devices[0], &error);
    if (!ups) {
      std::cerr << "open failed: " << error << "\n";
      return 1;
    }
    if (monitor) {
      return run_monitor(*ups, interval_ms, !every_poll, json);
    }
    return run_device_action(*ups, device_action, json);
  }

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
