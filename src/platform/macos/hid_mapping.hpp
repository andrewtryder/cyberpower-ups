#pragma once

// Maps IOKit HID elements onto the library's Status fields. Split out of
// hid_macos.cpp so the usage-mapping rules live apart from device I/O.

#include "cyberpower/ups.hpp"

#include <IOKit/hid/IOHIDLib.h>

#include <optional>
#include <string>

namespace cyberpower::platform {

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
const char* element_type_label(IOHIDElementType type);

// Folds one element into `r` (or directly into `status` for boolean flags).
// Returns a short label describing the mapping for --dump-raw, or nullptr when
// the element was ignored.
const char* map_element(IOHIDElementRef element, double value, Readings& r, Status& status);

// Converts the gathered readings into the public Status fields.
void apply_readings(const Readings& r, Status& status);

// One-line summary of the readings for --dump-raw.
std::string summarize(const Readings& r, const Status& status);

}  // namespace cyberpower::platform
