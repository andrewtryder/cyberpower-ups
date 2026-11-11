#pragma once

// Maps USB HID elements onto the library's Status fields. Platform-independent
// usage decoding logic shared across macOS, Linux, and offline test harnesses.

#include "cyberpower/ups.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cyberpower::platform {

enum class HidElementType {
  Input,
  Feature,
  Output,
  Other,
};

// Generic descriptor of a HID element needed for status decoding.
struct HidElement {
  uint32_t page = 0;
  uint32_t usage = 0;
  HidElementType type = HidElementType::Other;
  int logical_min = 0;
  int logical_max = 0;
  uint32_t report_id = 0;
  // Ancestor collections (page, usage) from innermost to outermost.
  std::vector<std::pair<uint32_t, uint32_t>> ancestors;

  bool ancestor_is(uint32_t p, uint32_t u) const {
    for (const auto& a : ancestors) {
      if (a.first == p && a.second == u) return true;
    }
    return false;
  }
};

// Values gathered while walking the HID element tree. Unset optionals mean
// "not exposed by this device".
struct Readings {
  std::optional<double> input_v, output_v, battery_v;
  std::optional<double> load, freq, temp;
  std::optional<double> remain, full, runtime;
  std::optional<double> cycle_count, sensitivity, shutdown_delay, restore_delay;
  std::optional<bool> need_replacement;
  int loose_voltage = 0;  // number of un-parented 0x30 voltages seen so far
};

// Short label ("In", "Feat", "Out", "Other") for --dump-raw traces.
const char* element_type_label(HidElementType type);

// Folds one element into `r` (or directly into `status` for boolean flags).
// Returns a short label describing the mapping for --dump-raw, or nullptr when
// the element was ignored.
const char* map_element(const HidElement& element, double value, Readings& r, Status& status);

// Converts the gathered readings into the public Status fields.
void apply_readings(const Readings& r, Status& status);

// One-line summary of the readings for --dump-raw.
std::string summarize(const Readings& r, const Status& status);

}  // namespace cyberpower::platform
