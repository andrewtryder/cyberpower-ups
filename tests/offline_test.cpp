#include "cyberpower/protocol.hpp"
#include "cyberpower/ups.hpp"
#include "internal/device_helpers.hpp"
#include "internal/parse_integer.hpp"
#include "internal/serial_discovery.hpp"
#include "internal/serial_write.hpp"
#include "internal/hid_mapping.hpp"
#include "internal/hid_linux_decode.hpp"
#include "internal/serial_posix.hpp"

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

bool test_hid_mapping() {
  using namespace cyberpower::platform;
  using namespace cyberpower::protocol;
  Readings r;
  cyberpower::Status status;

  // Voltage collection with parent Input (0x1A)
  HidElement in_elem;
  in_elem.page = kPagePowerDevice;
  in_elem.usage = 0x30;
  in_elem.ancestors = {{kPagePowerDevice, 0x1A}};
  if (std::string(map_element(in_elem, 120.0, r, status)) != "input_v") return fail("hid map input_v");
  if (!r.input_v || *r.input_v != 120.0) return fail("hid r.input_v");

  // Output voltage with parent Output (0x1C)
  HidElement out_elem;
  out_elem.page = kPagePowerDevice;
  out_elem.usage = 0x30;
  out_elem.ancestors = {{kPagePowerDevice, 0x1C}};
  if (std::string(map_element(out_elem, 119.5, r, status)) != "output_v") return fail("hid map output_v");
  if (!r.output_v || *r.output_v != 119.5) return fail("hid r.output_v");

  // Battery voltage with parent Battery (0x12)
  HidElement bat_elem;
  bat_elem.page = kPagePowerDevice;
  bat_elem.usage = 0x30;
  bat_elem.ancestors = {{kPagePowerDevice, 0x12}};
  if (std::string(map_element(bat_elem, 27.2, r, status)) != "battery_v") return fail("hid map battery_v");
  if (!r.battery_v || *r.battery_v != 27.2) return fail("hid r.battery_v");

  // Load percent (modern 0x65)
  HidElement load_elem;
  load_elem.page = kPagePowerDevice;
  load_elem.usage = kUsagePercentLoad;
  if (std::string(map_element(load_elem, 15.0, r, status)) != "load(65_new)") return fail("hid map load");

  // ACPresent (0xFD)
  HidElement ac_elem;
  ac_elem.page = kPagePowerDevice;
  ac_elem.usage = kUsageACPresent;
  if (std::string(map_element(ac_elem, 1.0, r, status)) != "ac_present") return fail("hid map ac_present");
  if (!status.ac_present || !*status.ac_present) return fail("status.ac_present");

  // Remaining capacity (0x2C)
  HidElement rem_elem;
  rem_elem.page = kPageBattery;
  rem_elem.usage = kUsageRemainingCapacity;
  if (std::string(map_element(rem_elem, 98.0, r, status)) != "remain(2C_new)") return fail("hid map remain");

  // Runtime to empty (0x8B)
  HidElement rt_elem;
  rt_elem.page = kPageBattery;
  rt_elem.usage = kUsageRunTimeToEmpty;
  if (std::string(map_element(rt_elem, 3600.0, r, status)) != "runtime(8B_new)") return fail("hid map runtime");

  // Vendor 0xFF86 config usages
  HidElement sens_elem;
  sens_elem.page = kPageVendorFf86;
  sens_elem.usage = kUsageVendorSensitivityRead;
  if (std::string(map_element(sens_elem, 2.0, r, status)) != "sensitivity(FF86)") return fail("hid map sens");

  HidElement shut_elem;
  shut_elem.page = kPageVendorFf86;
  shut_elem.usage = kUsageVendorShutdownDelay;
  if (std::string(map_element(shut_elem, 60.0, r, status)) != "shutdown_delay(FF86)") return fail("hid map shutdown");

  HidElement rest_elem;
  rest_elem.page = kPageVendorFf86;
  rest_elem.usage = kUsageVendorRestoreDelay;
  if (std::string(map_element(rest_elem, 30.0, r, status)) != "restore_delay(FF86)") return fail("hid map restore");

  apply_readings(r, status);
  if (!status.input_voltage_v || *status.input_voltage_v != 120.0) return fail("applied input_voltage_v");
  if (!status.output_voltage_v || *status.output_voltage_v != 119.5) return fail("applied output_voltage_v");
  if (!status.battery_voltage_v || *status.battery_voltage_v != 27.2) return fail("applied battery_voltage_v");
  if (!status.load_percent || *status.load_percent != 15.0) return fail("applied load_percent");
  if (!status.battery_percent || *status.battery_percent != 98.0) return fail("applied battery_percent");
  if (!status.runtime_seconds || *status.runtime_seconds != 3600.0) return fail("applied runtime_seconds");
  if (!status.voltage_sensitivity || *status.voltage_sensitivity != 2) return fail("applied voltage_sensitivity");
  if (!status.shutdown_delay_s || *status.shutdown_delay_s != 60.0) return fail("applied shutdown_delay_s");
  if (!status.restore_delay_s || *status.restore_delay_s != 30.0) return fail("applied restore_delay_s");

  return true;
}

bool test_safe_serial_discovery() {
  using cyberpower::platform::serial_port_name_is_candidate;

  // 1. Driver-named devices must ALWAYS be accepted (even without include_generic).
  if (!serial_port_name_is_candidate("cu.wchusbserial42", false) ||
      !serial_port_name_is_candidate("/dev/cu.wchusbserial1410", false) ||
      !serial_port_name_is_candidate("wchusbserial0", false)) {
    return fail("driver-named serial ports must be accepted by default");
  }

  // 2. Generic Linux ports (ttyS*, ttyUSB*, ttyACM*) MUST be rejected by default!
  const char* generic_linux[] = {
      "ttyS0", "ttyS1", "ttyS31", "/dev/ttyS0",
      "ttyUSB0", "ttyUSB1", "/dev/ttyUSB0",
      "ttyACM0", "ttyACM1", "/dev/ttyACM0"
  };
  for (const char* name : generic_linux) {
    if (serial_port_name_is_candidate(name, false)) {
      return fail(std::string("generic Linux port ") + name + " must be rejected without opt-in");
    }
    if (!serial_port_name_is_candidate(name, true)) {
      return fail(std::string("generic Linux port ") + name + " must be accepted with opt-in");
    }
  }

  // 3. Generic macOS ports (cu.usbserial*, cu.usbmodem*, cu.SLAB*) rejected by default, accepted with opt-in.
  const char* generic_mac[] = {
      "cu.usbserial-1420", "/dev/cu.usbserial-1420",
      "cu.usbmodem1430", "/dev/cu.usbmodem1430",
      "cu.SLAB_USBtoUART", "/dev/cu.SLAB_USBtoUART"
  };
  for (const char* name : generic_mac) {
    if (serial_port_name_is_candidate(name, false)) {
      return fail(std::string("generic macOS port ") + name + " must be rejected without opt-in");
    }
    if (!serial_port_name_is_candidate(name, true)) {
      return fail(std::string("generic macOS port ") + name + " must be accepted with opt-in");
    }
  }

  // 4. Completely unrelated devices must be rejected in all cases.
  const char* unrelated[] = {"random_device", "null", "zero", "tty", "console", "ptmx"};
  for (const char* name : unrelated) {
    if (serial_port_name_is_candidate(name, false) || serial_port_name_is_candidate(name, true)) {
      return fail(std::string("unrelated device ") + name + " must never be accepted");
    }
  }

  return true;
}

bool test_posix_termios_configuration() {
#if defined(__unix__) || defined(__APPLE__)
  using namespace cyberpower::platform;

  termios tio{};
  std::string err;

  // Invalid baud rejected
  if (configure_posix_termios(tio, 5000, err)) {
    return fail("unsupported baud accepted");
  }
  if (err.find("unsupported baud") == std::string::npos) {
    return fail("unexpected error message for invalid baud");
  }

  // Valid default baud 2400
  if (!configure_posix_termios(tio, 2400, err)) {
    return fail("configure_posix_termios failed: " + err);
  }

  // Verify POSIX flags
  if ((tio.c_cflag & CSIZE) != CS8) return fail("termios: not 8 data bits");
  if (tio.c_cflag & PARENB) return fail("termios: parity enabled");
  if (tio.c_cflag & CSTOPB) return fail("termios: two stop bits instead of one");
  if ((tio.c_cflag & (CLOCAL | CREAD)) != (CLOCAL | CREAD)) return fail("termios: CLOCAL/CREAD missing");
  if (tio.c_cc[VMIN] != 0 || tio.c_cc[VTIME] != 0) return fail("termios: nonblocking VMIN/VTIME not zero");
  if (cfgetispeed(&tio) != B2400 || cfgetospeed(&tio) != B2400) return fail("termios: baud not 2400");

  // Pseudo-terminal test: verify applying settings and round-trip communication
  const int master_fd = ::posix_openpt(O_RDWR | O_NOCTTY);
  if (master_fd >= 0) {
    if (::grantpt(master_fd) == 0 && ::unlockpt(master_fd) == 0) {
      char* slave_name = ::ptsname(master_fd);
      if (slave_name != nullptr) {
        const int slave_fd = ::open(slave_name, O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (slave_fd >= 0) {
          termios slave_tio{};
          if (::tcgetattr(slave_fd, &slave_tio) == 0) {
            if (configure_posix_termios(slave_tio, 2400, err)) {
              if (::tcsetattr(slave_fd, TCSANOW, &slave_tio) == 0) {
                termios verified{};
                if (::tcgetattr(slave_fd, &verified) == 0) {
                  if ((verified.c_cflag & CSIZE) != CS8 || (verified.c_cflag & PARENB) ||
                      (verified.c_cflag & CSTOPB)) {
                    ::close(slave_fd);
                    ::close(master_fd);
                    return fail("pty slave termios flags mismatch");
                  }
                }
                // Test basic data transmission through PTY pair
                const char tx_buf[] = "D\r";
                if (::write(slave_fd, tx_buf, sizeof(tx_buf) - 1) == sizeof(tx_buf) - 1) {
                  char rx_buf[16]{};
                  if (::read(master_fd, rx_buf, sizeof(rx_buf)) > 0) {
                    if (rx_buf[0] != 'D' || rx_buf[1] != '\r') {
                      ::close(slave_fd);
                      ::close(master_fd);
                      return fail("pty data mismatch");
                    }
                  }
                }
              }
            }
          }
          ::close(slave_fd);
        }
      }
    }
    ::close(master_fd);
  }
#endif
  return true;
}

bool test_hid_feature_validation_and_decoding() {
  using namespace cyberpower::platform;

  // Header validation
  uint8_t dummy[4] = {0x0B, 0x01, 0, 0};
  if (validate_feature_report(nullptr, 4, 0x0B, 2)) return fail("null buffer accepted");
  if (validate_feature_report(dummy, 1, 0x0B, 2)) return fail("truncated buffer accepted");
  if (validate_feature_report(dummy, 4, 0x0C, 2)) return fail("mismatched report ID accepted");
  if (!validate_feature_report(dummy, 2, 0x0B, 2)) return fail("valid feature report rejected");

  // Report 0x0B decoding
  Readings r;
  cyberpower::Status s;
  const uint8_t rep_0b_valid[2] = {0x0B, 0x01}; // AC present
  if (!decode_cp1500_report_0b(rep_0b_valid, 2, r, s)) return fail("decode report 0x0B valid");
  if (!s.ac_present || *s.ac_present != true) return fail("report 0x0B ac_present");
  if (s.charging && *s.charging != false) return fail("report 0x0B charging false");

  const uint8_t rep_0b_chg[2] = {0x0B, 0x07}; // AC + Chg + Dischg
  if (!decode_cp1500_report_0b(rep_0b_chg, 2, r, s)) return fail("decode report 0x0B chg");
  if (!s.charging || *s.charging != true) return fail("report 0x0B charging true");
  if (!s.discharging || *s.discharging != true) return fail("report 0x0B discharging true");

  const uint8_t rep_0b_trunc[1] = {0x0B};
  if (decode_cp1500_report_0b(rep_0b_trunc, 1, r, s)) return fail("truncated 0x0B accepted");

  // Report 0x08 decoding (battery % + runtime seconds)
  const uint8_t rep_08_valid[4] = {0x08, 100, 0x47, 0x11}; // 100%, 0x1147 = 4423 s
  if (!decode_cp1500_report_08(rep_08_valid, 4, r, s)) return fail("decode report 0x08 valid");
  if (!r.remain || *r.remain != 100.0) return fail("report 0x08 remain");
  if (!r.runtime || *r.runtime != 4423.0) return fail("report 0x08 runtime");

  // Out-of-range battery % (> 100) ignored
  r.remain.reset();
  const uint8_t rep_08_invalid_pct[4] = {0x08, 150, 0x47, 0x11};
  decode_cp1500_report_08(rep_08_invalid_pct, 4, r, s);
  if (r.remain.has_value()) return fail("report 0x08 invalid percent accepted");

  // Report 0x0F (input voltage)
  const uint8_t rep_0f_valid[3] = {0x0F, 121, 0}; // 121 V
  if (!decode_cp1500_report_0f(rep_0f_valid, 3, r, s)) return fail("decode report 0x0F valid");
  if (!r.input_v || *r.input_v != 121.0) return fail("report 0x0F input_v");

  // Report 0x12 (output voltage)
  const uint8_t rep_12_valid[3] = {0x12, 120, 0}; // 120 V
  if (!decode_cp1500_report_12(rep_12_valid, 3, r, s)) return fail("decode report 0x12 valid");
  if (!r.output_v || *r.output_v != 120.0) return fail("report 0x12 output_v");

  // Report 0x13 (load %)
  const uint8_t rep_13_valid[2] = {0x13, 9}; // 9%
  if (!decode_cp1500_report_13(rep_13_valid, 2, r, s)) return fail("decode report 0x13 valid");
  if (!r.load || *r.load != 9.0) return fail("report 0x13 load");

  // Report 0x0A (battery voltage in tenths)
  const uint8_t rep_0a_valid[3] = {0x0A, 20, 1}; // 276 tenths = 27.6 V
  if (!decode_cp1500_report_0a(rep_0a_valid, 3, r, s)) return fail("decode report 0x0A valid");
  if (!r.battery_v || !approx(*r.battery_v, 27.6)) return fail("report 0x0A battery_v");

  // Report 0x02 (sensitivity: 1..3)
  const uint8_t rep_02_valid[2] = {0x02, 2}; // Medium
  if (!decode_cp1500_report_02(rep_02_valid, 2, r, s)) return fail("decode report 0x02 valid");
  if (!r.sensitivity || *r.sensitivity != 2.0) return fail("report 0x02 sensitivity");

  const uint8_t rep_02_invalid[2] = {0x02, 5};
  if (decode_cp1500_report_02(rep_02_invalid, 2, r, s)) return fail("report 0x02 invalid accepted");

  return true;
}

bool test_battery_replacement_handling() {
  using namespace cyberpower::platform;

  Readings r;
  cyberpower::Status s;

  // Real hardware payload from CP1500PFCLCDa Report 0x07:
  // [0]=0x07, [1]=100, [2]=5, [3]=10, [4]=20, [5]=10, [6]=100
  const uint8_t rep_07_real[7] = {0x07, 0x64, 0x05, 0x0A, 0x14, 0x0A, 0x64};
  if (!decode_cp1500_report_07(rep_07_real, 7, r, s)) return fail("decode report 0x07");

  // Crucial check: need_replacement must NOT be set!
  apply_readings(r, s);
  if (s.need_replacement.has_value()) {
    return fail("report 0x07 must not assert need_replacement");
  }
  if (!s.cycle_count || *s.cycle_count != 10) {
    return fail("report 0x07 cycle count not mapped");
  }

  // Shared mapping tests for kUsageNeedReplacementStd (0x2B)
  r = Readings{};
  s = cyberpower::Status{};
  HidElement repl_elem;
  repl_elem.page = cyberpower::protocol::kPageBattery;
  repl_elem.usage = cyberpower::protocol::kUsageNeedReplacementStd; // 0x2B

  map_element(repl_elem, 1.0, r, s);
  apply_readings(r, s);
  if (!s.need_replacement || *s.need_replacement != true) {
    return fail("kUsageNeedReplacementStd 1.0 must set need_replacement true");
  }

  r = Readings{};
  s = cyberpower::Status{};
  map_element(repl_elem, 0.0, r, s);
  apply_readings(r, s);
  if (!s.need_replacement || *s.need_replacement != false) {
    return fail("kUsageNeedReplacementStd 0.0 must set need_replacement false");
  }

  r = Readings{};
  s = cyberpower::Status{};
  map_element(repl_elem, 20.0, r, s); // non-flag value
  apply_readings(r, s);
  if (s.need_replacement.has_value()) {
    return fail("kUsageNeedReplacementStd non-flag must not set need_replacement");
  }

  // 0x29 with non-flag threshold (e.g. 10.0 or 20.0) must not set need_replacement
  r = Readings{};
  s = cyberpower::Status{};
  HidElement cap_limit_elem;
  cap_limit_elem.page = cyberpower::protocol::kPageBattery;
  cap_limit_elem.usage = cyberpower::protocol::kUsageNeedReplacement; // 0x29
  map_element(cap_limit_elem, 10.0, r, s);
  apply_readings(r, s);
  if (s.need_replacement.has_value()) {
    return fail("kUsageNeedReplacement numeric threshold must not set need_replacement");
  }

  return true;
}

bool test_unsupported_device_profile_and_controls() {
  using namespace cyberpower::platform;

  // Profile validation
  if (!is_verified_hid_profile(0x0764, 0x0601)) return fail("CP1500 profile must be verified");
  if (is_verified_hid_profile(0x0764, 0x0501)) return fail("unverified PID 0x0501 must be false");
  if (is_verified_hid_profile(0x1234, 0x0601)) return fail("unverified VID must be false");

  // Control value range validation
  for (int v : {1, 2, 3}) {
    if (!is_valid_cp1500_alarm_control(v)) return fail("valid alarm control rejected: " + std::to_string(v));
    if (!is_valid_cp1500_test_mode(v)) return fail("valid test mode rejected: " + std::to_string(v));
  }
  for (int v : {-1, 0, 4, 10, 99}) {
    if (is_valid_cp1500_alarm_control(v)) return fail("invalid alarm control accepted: " + std::to_string(v));
    if (is_valid_cp1500_test_mode(v)) return fail("invalid test mode accepted: " + std::to_string(v));
  }

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
      !test_toggle_buzzer() || !test_hid_mapping() || !test_safe_serial_discovery() ||
      !test_posix_termios_configuration() || !test_hid_feature_validation_and_decoding() ||
      !test_battery_replacement_handling() || !test_unsupported_device_profile_and_controls()) return 1;
  std::cout << "offline_test: regression helpers ok\n";

  // Always try the synthetic example shipped in-tree.
  if (!maybe_load_json_fixture(fixture_path("synthetic_status.json"))) return 1;
  // Optional real-device capture; skipped when absent.
  if (!maybe_load_json_fixture(fixture_path("cp1500pfclcda_status.json"))) return 1;

  std::cout << "offline_test: all checks passed\n";
  return 0;
}
