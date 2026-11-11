#include "cpups_args.hpp"
#include "cpups_format.hpp"

#include "cyberpower/protocol.hpp"
#include "cyberpower/ups.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) { g_stop.store(true, std::memory_order_relaxed); }

int run_device_action(cyberpower::Ups& ups, DeviceAction action, int param, bool json) {
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
    case DeviceAction::SetSensitivity: error = ups.set_voltage_sensitivity(param); break;
    case DeviceAction::SetShutdownDelay: error = ups.set_shutdown_delay(param); break;
    case DeviceAction::SetRestoreDelay: error = ups.set_restore_delay(param); break;
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
  Options options;
  const int parsed = parse_args(argc, argv, options);
  if (parsed >= 0) return parsed;
  const bool json = options.json;
  const bool monitor = options.monitor;
  const DeviceAction device_action = options.device_action;

  std::string detail;
  if (!cyberpower::protocol::self_test(&detail)) {
    std::cerr << "protocol self-test failed: " << detail << "\n";
    return 1;
  }
  if (options.self_test_only) {
    if (json) {
      std::cout << "{\"self_test\":\"ok\"}\n";
    } else {
      std::cout << "protocol self-test passed\n";
    }
    return 0;
  }

  if (options.dump_raw) {
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
      return run_monitor(*ups, options.interval_ms, !options.every_poll, json);
    }
    return run_device_action(*ups, device_action, options.action_param, json);
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
