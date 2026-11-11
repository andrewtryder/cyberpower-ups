#include "cpups_args.hpp"

#include "internal/parse_integer.hpp"

#include <climits>
#include <iostream>

namespace {

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

// Consumes the value following a "--set-*" option, validating it against
// [min_value, max_value]. Returns false (after printing a diagnostic) on error.
bool parse_action_param(int argc, char** argv, int& i, const char* option, int min_value,
                        int max_value, const char* expectation, int& out) {
  if (i + 1 >= argc) {
    usage(argv[0]);
    return false;
  }
  if (!cyberpower::internal::parse_int_strict(argv[++i], out) || out < min_value ||
      out > max_value) {
    std::cerr << option << ": " << expectation << "\n";
    return false;
  }
  return true;
}

bool set_device_action(DeviceAction& current, DeviceAction next) {
  if (current != DeviceAction::None && current != next) {
    std::cerr << "only one device command may be specified at a time\n";
    return false;
  }
  current = next;
  return true;
}

}  // namespace

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


int parse_args(int argc, char** argv, Options& options) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--self-test") {
      options.self_test_only = true;
    } else if (arg == "--json") {
      options.json = true;
    } else if (arg == "--monitor") {
      options.monitor = true;
    } else if (arg == "--every-poll") {
      options.every_poll = true;
    } else if (arg == "--dump-raw") {
      options.dump_raw = true;
    } else if (arg == "--interval") {
      if (i + 1 >= argc) {
        usage(argv[0]);
        return 2;
      }
      if (!cyberpower::internal::parse_int_strict(argv[++i], options.interval_ms) || options.interval_ms <= 0) {
        std::cerr << "invalid --interval; expected positive milliseconds\n";
        return 2;
      }
    } else if (arg == "--test" || arg == "--self-test-device") {
      if (!set_device_action(options.device_action, DeviceAction::SelfTest)) return 2;
    } else if (arg == "--cancel-test") {
      if (!set_device_action(options.device_action, DeviceAction::CancelTest)) return 2;
    } else if (arg == "--buzzer" || arg == "--beep") {
      if (!set_device_action(options.device_action, DeviceAction::ToggleBuzzer)) return 2;
    } else if (arg == "--mute") {
      if (!set_device_action(options.device_action, DeviceAction::MuteAlarm)) return 2;
    } else if (arg == "--enable-alarm") {
      if (!set_device_action(options.device_action, DeviceAction::EnableAlarm)) return 2;
    } else if (arg == "--disable-alarm") {
      if (!set_device_action(options.device_action, DeviceAction::DisableAlarm)) return 2;
    } else if (arg == "--rating") {
      if (!set_device_action(options.device_action, DeviceAction::Rating)) return 2;
    } else if (arg == "--cancel-schedule") {
      if (!set_device_action(options.device_action, DeviceAction::CancelSchedule)) return 2;
    } else if (arg == "--calibrate") {
      if (!set_device_action(options.device_action, DeviceAction::Calibrate)) return 2;
    } else if (arg == "--indicator-test") {
      if (!set_device_action(options.device_action, DeviceAction::IndicatorTest)) return 2;
    } else if (arg == "--buzzer-test") {
      if (!set_device_action(options.device_action, DeviceAction::BuzzerTest)) return 2;
    } else if (arg == "--set-sensitivity") {
      if (!parse_action_param(argc, argv, i, "--set-sensitivity", 1, 3,
                              "value must be 1 (High), 2 (Medium), or 3 (Low)", options.action_param) ||
          !set_device_action(options.device_action, DeviceAction::SetSensitivity)) return 2;
    } else if (arg == "--set-shutdown-delay") {
      if (!parse_action_param(argc, argv, i, "--set-shutdown-delay", 0, INT_MAX,
                              "value must be non-negative seconds", options.action_param) ||
          !set_device_action(options.device_action, DeviceAction::SetShutdownDelay)) return 2;
    } else if (arg == "--set-restore-delay") {
      if (!parse_action_param(argc, argv, i, "--set-restore-delay", 0, INT_MAX,
                              "value must be non-negative seconds", options.action_param) ||
          !set_device_action(options.device_action, DeviceAction::SetRestoreDelay)) return 2;
    } else if (arg == "-h" || arg == "--help") {
      usage(argv[0]);
      return 0;
    } else {
      usage(argv[0]);
      return 2;
    }
  }

  if (options.monitor && options.device_action != DeviceAction::None) {
    std::cerr << "--monitor cannot be combined with a device command\n";
    return 2;
  }
  return -1;
}
