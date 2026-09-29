#include "cyberpower/protocol.hpp"
#include "cyberpower/ups.hpp"
#include "internal/device_helpers.hpp"
#include "internal/parse_integer.hpp"
#include "internal/serial_discovery.hpp"
#include "internal/serial_write.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <fcntl.h>
#include <unistd.h>

namespace {

bool fail(const std::string& why) {
  std::cerr << "offline_test: " << why << "\n";
  return false;
}

bool approx(double value, double expected, double eps = 0.05) {
  return std::fabs(value - expected) <= eps;
}

bool check_frame(const std::string& wire, char tag, int32_t expected_stored) {
  const cyberpower::protocol::StatusFrame frame = cyberpower::protocol::parse_v2e_status(wire, false);
  if (!frame.ok) {
    return fail("frame rejected (" + wire + "): error " +
                std::to_string(static_cast<int>(frame.error)));
  }
  const auto it = frame.fields.find(tag);
  if (it == frame.fields.end()) return fail(std::string("missing tag ") + tag);
  if (it->second.stored != expected_stored) {
    return fail(std::string("tag ") + tag + " stored " + std::to_string(it->second.stored) +
                " expected " + std::to_string(expected_stored));
  }
  return true;
}

bool test_hardcoded_frames() {
  // Same shape as the in-library sample, plus a second frame with different load/battery.
  const std::string frame_a = "#I120.0O120.0L010B095T025.0F60.0H027.0R030C0\r";
  if (!check_frame(frame_a, 'I', 120000)) return false;
  if (!check_frame(frame_a, 'O', 120000)) return false;
  if (!check_frame(frame_a, 'L', 10000)) return false;
  if (!check_frame(frame_a, 'B', 95000)) return false;
  if (!check_frame(frame_a, 'R', 1800)) return false;  // 30 minutes -> seconds

  const std::string frame_b = "#I230.5O230.0L100B050F50.0R005C1\r";
  if (!check_frame(frame_b, 'I', 230500)) return false;
  if (!check_frame(frame_b, 'L', 100000)) return false;
  if (!check_frame(frame_b, 'B', 50000)) return false;
  if (!check_frame(frame_b, 'R', 300)) return false;  // 5 minutes -> seconds
  if (!check_frame(frame_b, 'C', 60)) return false;   // 1 minute -> seconds

  // Engineering units via nominal_from_stored.
  const auto parsed = cyberpower::protocol::parse_v2e_status(frame_a, false);
  const double battery = cyberpower::protocol::nominal_from_stored('B', parsed.fields.at('B').stored);
  if (!approx(battery, 95.0)) return fail("battery nominal");
  return true;
}

bool test_malformed_numbers() {
  using cyberpower::Error;
  for (const std::string& wire : {"#B\r", "#I\r", "#R\r", "#B1.\r", "#B.1\r",
                                  "#B999999999999999999999999\r", "#B9999999k\r"}) {
    if (cyberpower::protocol::parse_v2e_status(wire, false).error != Error::RespNotNumber) {
      return fail("malformed numeric field accepted: " + wire);
    }
  }
  const auto outlet = cyberpower::protocol::parse_v2e_status("#W\r", false);
  return outlet.ok && outlet.outlet_state.empty() ? true : fail("empty W tail rejected");
}

bool test_pure_helpers() {
  int value = 0;
  if (!cyberpower::internal::parse_int_strict("123", value) || value != 123 ||
      cyberpower::internal::parse_int_strict("", value) ||
      cyberpower::internal::parse_int_strict("12x", value) ||
      cyberpower::internal::parse_int_strict("999999999999999999999", value)) {
    return fail("strict integer parsing");
  }
  if (!cyberpower::platform::serial_port_name_is_candidate("cu.wchusbserial42", false) ||
      cyberpower::platform::serial_port_name_is_candidate("cu.usbmodem123", false) ||
      !cyberpower::platform::serial_port_name_is_candidate("cu.usbmodem123", true)) {
    return fail("serial discovery filtering");
  }
  if (!cyberpower::protocol::battery_usage_67_is_legacy_full_capacity(0, 1000) ||
      cyberpower::protocol::battery_usage_67_is_legacy_full_capacity(0, 100)) {
    return fail("battery usage 0x67 descriptor disambiguation");
  }
  return true;
}

bool test_nonblocking_write_timeout() {
  int pipefd[2] = {-1, -1};
  if (::pipe(pipefd) != 0) return fail("pipe setup");
  const int flags = ::fcntl(pipefd[1], F_GETFL, 0);
  if (flags < 0 || ::fcntl(pipefd[1], F_SETFL, flags | O_NONBLOCK) != 0) {
    ::close(pipefd[0]);
    ::close(pipefd[1]);
    return fail("pipe nonblocking setup");
  }
  char fill[4096]{};
  while (::write(pipefd[1], fill, sizeof fill) > 0) {}
  const auto start = std::chrono::steady_clock::now();
  const auto result = cyberpower::platform::write_all_nonblocking(pipefd[1], "x", 1, 20);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  ::close(pipefd[0]);
  ::close(pipefd[1]);
  if (result != cyberpower::Error::Timeout || elapsed > std::chrono::seconds(1)) {
    return fail("nonblocking write timeout");
  }
  return true;
}

class MockTransport final : public cyberpower::platform::Transport {
 public:
  cyberpower::DeviceInfo device_info;
  cyberpower::Error read_result = cyberpower::Error::NotSupported;
  int alarm_value = cyberpower::protocol::kAlarmEnable;
  cyberpower::Error set_result = cyberpower::Error::Ok;
  cyberpower::Error transact_result = cyberpower::Error::Ok;
  int set_calls = 0;
  int transact_calls = 0;
  int last_set = 0;
  const cyberpower::DeviceInfo& info() const override { return device_info; }
  cyberpower::Status read_status() override { return {}; }
  cyberpower::Error transact(const std::string&, std::string&) override { ++transact_calls; return transact_result; }
  cyberpower::Error get_alarm_control(int& value) override { value = alarm_value; return read_result; }
  cyberpower::Error set_alarm_control(int value) override { ++set_calls; last_set = value; return set_result; }
};

bool test_toggle_buzzer() {
  MockTransport mock;
  mock.read_result = cyberpower::Error::Ok;
  if (cyberpower::internal::toggle_buzzer(mock) != cyberpower::Error::Ok || mock.set_calls != 1 ||
      mock.last_set != cyberpower::protocol::kAlarmMute || mock.transact_calls != 0) return fail("toggle HID success");
  mock = MockTransport{};
  mock.read_result = cyberpower::Error::NotSupported;
  if (cyberpower::internal::toggle_buzzer(mock) != cyberpower::Error::Ok || mock.set_calls != 0 ||
      mock.transact_calls != 1) return fail("toggle serial fallback");
  mock = MockTransport{};
  mock.read_result = cyberpower::Error::Io;
  if (cyberpower::internal::toggle_buzzer(mock) != cyberpower::Error::Io || mock.set_calls != 0 ||
      mock.transact_calls != 0) return fail("toggle propagates HID I/O error");
  return true;
}

bool extract_json_number(const std::string& text, const std::string& key, double& out) {
  const std::string needle = "\"" + key + "\":";
  const auto pos = text.find(needle);
  if (pos == std::string::npos) return false;
  std::size_t i = pos + needle.size();
  while (i < text.size() && (text[i] == ' ' || text[i] == '\t')) ++i;
  if (i < text.size() && text.compare(i, 4, "null") == 0) return false;
  try {
    std::size_t consumed = 0;
    out = std::stod(text.substr(i), &consumed);
    return consumed > 0;
  } catch (...) {
    return false;
  }
}

bool maybe_load_json_fixture(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    std::cout << "offline_test: optional fixture not present (" << path << "); skipped\n";
    return true;
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string text = buffer.str();
  if (text.find("\"devices\"") == std::string::npos &&
      text.find("\"status\"") == std::string::npos &&
      text.find("battery_percent") == std::string::npos) {
    return fail("fixture " + path + " does not look like cpups --json output");
  }

  double battery = 0;
  if (extract_json_number(text, "battery_percent", battery)) {
    if (battery < 0.0 || battery > 100.0) {
      return fail("fixture battery_percent out of range: " + std::to_string(battery));
    }
  }
  double load = 0;
  if (extract_json_number(text, "load_percent", load)) {
    if (load < 0.0 || load > 200.0) {
      return fail("fixture load_percent out of range: " + std::to_string(load));
    }
  }
  // Extended fields: validate range when present.
  double cycle = 0;
  if (extract_json_number(text, "cycle_count", cycle)) {
    if (cycle < 0.0 || cycle > 10000.0) {
      return fail("fixture cycle_count out of range: " + std::to_string(cycle));
    }
  }
  double sens = 0;
  if (extract_json_number(text, "voltage_sensitivity", sens)) {
    if (sens < 1.0 || sens > 3.0) {
      return fail("fixture voltage_sensitivity out of range: " + std::to_string(sens));
    }
  }
  double shutd = 0;
  if (extract_json_number(text, "shutdown_delay_s", shutd)) {
    if (shutd < 0.0) {
      return fail("fixture shutdown_delay_s negative: " + std::to_string(shutd));
    }
  }
  double rest = 0;
  if (extract_json_number(text, "restore_delay_s", rest)) {
    if (rest < 0.0) {
      return fail("fixture restore_delay_s negative: " + std::to_string(rest));
    }
  }
  std::cout << "offline_test: loaded fixture " << path << "\n";
  return true;
}

bool test_protocol_constants() {
  using namespace cyberpower::protocol;
  // Vendor 0xFF86 sensitivity usages (High confidence).
  if (kUsageVendorSensitivityRead  != 0x0061) return fail("kUsageVendorSensitivityRead");
  if (kUsageVendorSensitivityWrite != 0x0072) return fail("kUsageVendorSensitivityWrite");
  if (kUsageVendorShutdownDelay    != 0x0016) return fail("kUsageVendorShutdownDelay");
  if (kUsageVendorRestoreDelay     != 0x0052) return fail("kUsageVendorRestoreDelay");
  // Sensitivity encoding values (Medium confidence; check symbolic names).
  if (kSensitivityHigh   != 1) return fail("kSensitivityHigh");
  if (kSensitivityMedium != 2) return fail("kSensitivityMedium");
  if (kSensitivityLow    != 3) return fail("kSensitivityLow");
  // RE-confirmed Battery System usages.
  if (kUsageRemainingCapacity  != 0x002C) return fail("kUsageRemainingCapacity");
  if (kUsageRunTimeToEmpty     != 0x008B) return fail("kUsageRunTimeToEmpty");
  if (kUsageCycleCount         != 0x008C) return fail("kUsageCycleCount");
  if (kUsageNeedReplacement    != 0x0029) return fail("kUsageNeedReplacement");
  if (kUsageCharging           != 0x0068) return fail("kUsageCharging");
  if (kUsageDischarging        != 0x0066) return fail("kUsageDischarging");
  // RE-confirmed Power Device usages.
  if (kUsagePercentLoad   != 0x0065) return fail("kUsagePercentLoad");
  if (kUsageACPresent     != 0x00FD) return fail("kUsageACPresent");
  if (kUsageFirmwareVer   != 0x00FE) return fail("kUsageFirmwareVer");
  return true;
}

std::string fixture_path(const char* name) {
  // Prefer source-tree fixtures/ when CTest sets CPUPS_FIXTURES_DIR.
  if (const char* root = std::getenv("CPUPS_FIXTURES_DIR")) {
    return std::string(root) + "/" + name;
  }
  return std::string("fixtures/") + name;
}

}  // namespace

int main() {
  std::string detail;
  if (!cyberpower::protocol::self_test(&detail)) {
    fail("protocol::self_test: " + detail);
    return 1;
  }
  std::cout << "offline_test: protocol::self_test ok\n";

  if (!test_hardcoded_frames()) return 1;
  std::cout << "offline_test: hardcoded v2e frames ok\n";

  if (!test_protocol_constants()) return 1;
  std::cout << "offline_test: protocol constants ok\n";

  if (!test_malformed_numbers() || !test_pure_helpers() || !test_nonblocking_write_timeout() ||
      !test_toggle_buzzer()) return 1;
  std::cout << "offline_test: regression helpers ok\n";

  // Always try the synthetic example shipped in-tree.
  if (!maybe_load_json_fixture(fixture_path("synthetic_status.json"))) return 1;
  // Optional real-device capture; skipped when absent.
  if (!maybe_load_json_fixture(fixture_path("cp1500pfclcda_status.json"))) return 1;

  std::cout << "offline_test: all checks passed\n";
  return 0;
}
