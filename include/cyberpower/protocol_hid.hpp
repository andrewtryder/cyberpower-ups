#pragma once

// HID usage pages, usages, control values and the recovered status-usage table.
// Provenance tags (High / Medium / Low / Speculative) are defined in protocol.hpp.

#include <cstdint>

namespace cyberpower::protocol {

// 0x85/0x67 is Temperature in RE-confirmed modern descriptors, but older
// firmware used it for FullChargeCapacity. A descriptor whose logical maximum
// exceeds a plausible temperature range is treated as the legacy capacity.
// Medium: compatibility rule based on descriptor metadata, not value alone.
bool battery_usage_67_is_legacy_full_capacity(int logical_min, int logical_max);

// ---------------------------------------------------------------------------
// HID. Vendor 0x0764 is CyberPower. High (mov edi, 0x764 next to product ids).
// Matching dictionaries are built for product ids 0x0005, 0x0501, 0x0601.
// UsageMapping additionally branches on product id 0x051D. High.
// ---------------------------------------------------------------------------

constexpr uint16_t kVendorId = 0x0764;
constexpr uint16_t kKnownProductIds[] = {0x0005, 0x0501, 0x0601, 0x051D};

// Usage pages seen as the high word of each mapped element. High.
constexpr uint16_t kPagePowerDevice = 0x0084;
constexpr uint16_t kPageBattery = 0x0085;
constexpr uint16_t kPageVendorFf86 = 0xFF86;
constexpr uint16_t kPageVendorFf01 = 0xFF01;

// ---------------------------------------------------------------------------
// Power Device (0x84) usages recovered from UsageMapping::Initialize. High.
// ---------------------------------------------------------------------------

// Writable controls (High page/usage). Report IDs come from the descriptor;
// these are HID usages, NOT report IDs — they are distinct.
constexpr uint16_t kUsageAudibleAlarmControl = 0x005A;  // 0x84/0x5A
constexpr uint16_t kUsageTest = 0x0058;                  // 0x84/0x58

// Read-only status usages (High).
constexpr uint16_t kUsageVoltage        = 0x0030;  // Voltage (parent coll selects I/O/bat)
constexpr uint16_t kUsageFrequency      = 0x0032;  // Frequency
constexpr uint16_t kUsagePercentLoad    = 0x0065;  // PercentLoad — High from UsageMapping
constexpr uint16_t kUsageTemperaturePD  = 0x0036;  // Temperature (Power Device page)
constexpr uint16_t kUsageACPresent      = 0x00FD;  // ACPresent (also on 0xFF01/0xD0)
constexpr uint16_t kUsageFirmwareVer    = 0x00FE;  // Firmware version string / iProduct

// Audible Alarm Control values (High for 1..3).
constexpr int kAlarmDisable = 1;
constexpr int kAlarmEnable = 2;
constexpr int kAlarmMute = 3;

// HidUps::TEST_MODE immediates (Medium English names).
constexpr int kTestQuick = 1;
constexpr int kTestDeep = 2;
constexpr int kTestAbort = 3;

// Feature report ID fallbacks for product id 0x0601 only (High from descriptor).
constexpr uint8_t kPid0601AlarmReportId = 0x0C;
constexpr uint8_t kPid0601TestReportId = 0x14;
constexpr uint16_t kPidCp1500Pfclcda = 0x0601;
constexpr uint16_t kPid0601 = 0x0601;

// ---------------------------------------------------------------------------
// Battery System (0x85) usages recovered from UsageMapping::Initialize. High.
// ---------------------------------------------------------------------------

constexpr uint16_t kUsageRemainingCapacity    = 0x002C;  // RemainingCapacity (percent or raw)
constexpr uint16_t kUsageRunTimeToEmpty       = 0x008B;  // RunTimeToEmpty (seconds)
constexpr uint16_t kUsageFullChargeCapacity   = 0x008D;  // FullChargeCapacity
constexpr uint16_t kUsageDesignCapacity       = 0x008E;  // DesignCapacity
constexpr uint16_t kUsageCycleCount           = 0x008C;  // CycleCount
constexpr uint16_t kUsageTemperatureBat       = 0x0067;  // Temperature (Battery page)
constexpr uint16_t kUsageNeedReplacement      = 0x0029;  // NeedReplacement / RemainingCapacityLimit (0x85/0x29)
constexpr uint16_t kUsageRemainingCapacityLimit = 0x0029;  // RemainingCapacityLimit (percent threshold)
constexpr uint16_t kUsageNeedReplacementStd   = 0x002B;  // NeedReplacement (USB HID Power Device 1.12 std)
constexpr uint16_t kUsageCharging             = 0x0068;  // Charging (bit)
constexpr uint16_t kUsageDischarging          = 0x0066;  // Discharging (bit)

// Older / alternative Battery System usages that may appear on some firmware
// revisions (present in prior mapping, kept for compatibility). Medium.
constexpr uint16_t kUsageRemCapAlt  = 0x0066;  // RemainingCapacity (alternate, pre-RE)
constexpr uint16_t kUsageFullCapAlt = 0x0067;  // FullChargeCapacity (alternate)
constexpr uint16_t kUsageRuntimeAlt = 0x0068;  // RunTimeToEmpty (alternate)
constexpr uint16_t kUsageChargingAlt  = 0x0044;  // Charging (alternate / older firmware)
constexpr uint16_t kUsageDischargingAlt = 0x0045;  // Discharging (alternate)
constexpr uint16_t kUsageACPresentAlt   = 0x00D0;  // ACPresent (Battery / 0xFF01 pages)
constexpr uint16_t kUsageChargingAlt2   = 0x00D1;  // Charging (0xFF01)
constexpr uint16_t kUsageDischargingAlt2 = 0x00D2; // Discharging (0xFF01)

// ---------------------------------------------------------------------------
// Vendor page 0xFF86 — CyberPower proprietary config usages. High page/usage;
// confidence tags on individual usages noted below.
// IMPORTANT: these are HID usage numbers, NOT HID report IDs. Report IDs for
// these usages on PID 0x0601 must be read from the device descriptor at
// runtime via IOHIDElementGetReportID — do not hardcode them.
// ---------------------------------------------------------------------------

// Voltage sensitivity (input transfer threshold preset).
//   Read : page 0xFF86 / usage 0x61 (High — extracted from Initialize movw).
//   Write: page 0xFF86 / usage 0x72 (High — SetupVoltageSensitivity path).
//   Values: 1=High sensitivity, 2=Medium, 3=Low (Medium — inferred from
//           3-value alarm pattern; not confirmed by disasm alone).
constexpr uint16_t kUsageVendorSensitivityRead  = 0x0061;
constexpr uint16_t kUsageVendorSensitivityWrite = 0x0072;
constexpr int kSensitivityHigh   = 1;  // Medium confidence on value encoding
constexpr int kSensitivityMedium = 2;
constexpr int kSensitivityLow    = 3;

// Shutdown / restore delay timers. Units: seconds (High — HidUps::Sleep(int)
// confirmed to pass seconds directly).
//   Shutdown delay read/write : page 0xFF86 / usage 0x16 (High).
//   Restore/startup delay r/w : page 0xFF86 / usage 0x52 (High).
constexpr uint16_t kUsageVendorShutdownDelay = 0x0016;
constexpr uint16_t kUsageVendorRestoreDelay  = 0x0052;

// Second config-write slot paired with sensitivity write in Initialize (High
// that both appear, Low for the full semantic). Not currently used for writes.
constexpr uint16_t kUsageVendorConfigParam = 0x0042;

// Usages that both appear in UsageMapping::Initialize and have a standard
// USB HID Power Device / Battery System name. The pair (page, usage) is
// High. The English name is the HID Usage Tables name (Medium: the binary
// does not store the English string, but the numeric pair matches the spec).
struct HidUsage {
  uint16_t page;
  uint16_t usage;
  const char* name;  // HID Usage Tables name
};

// Usages used to build a status snapshot.
// Column 1: page. Column 2: usage. Column 3: USB HID name (Medium for name;
// High for the (page,usage) pair extracted from UsageMapping::Initialize).
constexpr HidUsage kStatusUsages[] = {
    // Power Device — structural / collection usages
    {0x0084, 0x0004, "UPS"},
    {0x0084, 0x0012, "Battery"},
    {0x0084, 0x001A, "Input"},
    {0x0084, 0x001C, "Output"},
    {0x0084, 0x0024, "PowerSummary"},
    {0x0084, 0x0040, "ConfigVoltage"},
    {0x0084, 0x0042, "ConfigFrequency"},
    // Power Device — numeric readings
    {0x0084, 0x0030, "Voltage"},
    {0x0084, 0x0032, "Frequency"},
    {0x0084, 0x0035, "PercentLoad"},    // older firmware; 0x65 is RE-confirmed
    {0x0084, 0x0065, "PercentLoad"},    // High — confirmed in UsageMapping::Initialize
    {0x0084, 0x0036, "Temperature"},
    // Power Device — status bits (RE-confirmed from Initialize)
    {0x0084, 0x00FD, "ACPresent"},      // High
    {0x0084, 0x00FE, "FirmwareVersion"},// High — iProduct / string usage
    // Battery System — RE-confirmed primary usages from UsageMapping
    {0x0085, 0x002C, "RemainingCapacity"},   // High
    {0x0085, 0x008B, "RunTimeToEmpty"},      // High
    {0x0085, 0x008C, "CycleCount"},          // High
    {0x0085, 0x008D, "FullChargeCapacity"},  // High
    {0x0085, 0x008E, "DesignCapacity"},      // High
    {0x0085, 0x0067, "Temperature"},         // High — Battery System temperature
    {0x0085, 0x0029, "NeedReplacement"},     // High
    {0x0085, 0x0068, "Charging"},            // High (also 0x0044 on older fw)
    {0x0085, 0x0066, "Discharging"},         // High (also 0x0045 on older fw)
    // Battery System — alternate usages (older firmware / kept for compat)
    {0x0085, 0x0066, "RemainingCapacityAlt"},
    {0x0085, 0x0067, "FullChargeCapacityAlt"},
    {0x0085, 0x0068, "RunTimeToEmptyAlt"},
    {0x0085, 0x0044, "Charging"},
    {0x0085, 0x0045, "Discharging"},
    {0x0085, 0x00D0, "ACPresent"},           // also on 0xFF01
    {0x0085, 0x00D1, "BatteryCharging"},     // also on 0xFF01
    {0x0085, 0x00D2, "BatteryDischarging"},
    {0xFF01, 0x00D0, "ACPresentVendor"},
    {0xFF01, 0x00D1, "ChargingVendor"},
    {0xFF01, 0x00D2, "DischargingVendor"},
    // Vendor 0xFF86 — config / delay usages
    {0xFF86, 0x0061, "VoltageSensitivity"}, // High — read
    {0xFF86, 0x0016, "ShutdownDelay"},      // High
    {0xFF86, 0x0052, "RestoreDelay"},       // High
};

}  // namespace cyberpower::protocol
