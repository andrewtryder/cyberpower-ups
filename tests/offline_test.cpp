#include "cyberpower/protocol.hpp"
#include "cyberpower/ups.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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
  if (text.find("\"devices\"") == std::string::npos && text.find("\"status\"") == std::string::npos &&
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
  std::cout << "offline_test: loaded fixture " << path << "\n";
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

  // Always try the synthetic example shipped in-tree.
  if (!maybe_load_json_fixture(fixture_path("synthetic_status.json"))) return 1;
  // Optional real-device capture; skipped when absent.
  if (!maybe_load_json_fixture(fixture_path("cp1500pfclcda_status.json"))) return 1;

  std::cout << "offline_test: all checks passed\n";
  return 0;
}
