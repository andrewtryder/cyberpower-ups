#include "internal/transport.hpp"

#include "cyberpower/protocol.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace cyberpower::platform {
namespace {

std::string cf_string(CFTypeRef value) {
  if (value == nullptr || CFGetTypeID(value) != CFStringGetTypeID()) return {};
  char buf[512];
  if (!CFStringGetCString(static_cast<CFStringRef>(value), buf, sizeof buf, kCFStringEncodingUTF8)) {
    return {};
  }
  return buf;
}

int cf_int(CFTypeRef value) {
  if (value == nullptr || CFGetTypeID(value) != CFNumberGetTypeID()) return 0;
  int out = 0;
  CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberIntType, &out);
  return out;
}

bool ancestor_is(IOHIDElementRef element, uint32_t page, uint32_t usage) {
  for (IOHIDElementRef parent = IOHIDElementGetParent(element); parent != nullptr;
       parent = IOHIDElementGetParent(parent)) {
    if (IOHIDElementGetUsagePage(parent) == page && IOHIDElementGetUsage(parent) == usage) return true;
  }
  return false;
}

bool read_physical(IOHIDDeviceRef device, IOHIDElementRef element, double& out) {
  IOHIDValueRef value = nullptr;
  if (IOHIDDeviceGetValue(device, element, &value) != kIOReturnSuccess || value == nullptr) return false;
  out = IOHIDValueGetScaledValue(value, kIOHIDValueScaleTypePhysical);
  const CFIndex logical = IOHIDValueGetIntegerValue(value);
  // Some CyberPower reports leave the unit exponent at 0, so the physical
  // scale collapses to 0 while the logical value is the real reading.
  // Medium: fall back only in that case.
  if (out == 0.0 && logical != 0) out = static_cast<double>(logical);
  return true;
}

// Prefer Feature, then Output, for writable control elements.
IOHIDElementRef find_control_element(IOHIDDeviceRef device, uint32_t page, uint32_t usage) {
  CFArrayRef elements = IOHIDDeviceCopyMatchingElements(device, nullptr, kIOHIDOptionsTypeNone);
  if (elements == nullptr) return nullptr;

  IOHIDElementRef best = nullptr;
  IOHIDElementType best_type = kIOHIDElementTypeInput_Misc;
  const CFIndex count = CFArrayGetCount(elements);
  for (CFIndex i = 0; i < count; ++i) {
    auto* element = static_cast<IOHIDElementRef>(const_cast<void*>(CFArrayGetValueAtIndex(elements, i)));
    if (IOHIDElementGetUsagePage(element) != page || IOHIDElementGetUsage(element) != usage) continue;
    const IOHIDElementType type = IOHIDElementGetType(element);
    if (type != kIOHIDElementTypeFeature && type != kIOHIDElementTypeOutput) continue;
    if (best == nullptr || (type == kIOHIDElementTypeFeature && best_type != kIOHIDElementTypeFeature)) {
      best = element;
      best_type = type;
    }
  }
  if (best != nullptr) CFRetain(best);
  CFRelease(elements);
  return best;
}

class HidTransport : public Transport {
 public:
  HidTransport(IOHIDManagerRef manager, IOHIDDeviceRef device, DeviceInfo info)
      : manager_(manager), device_(device), info_(std::move(info)) {
    CFRetain(manager_);
    CFRetain(device_);
    alarm_element_ = find_control_element(device_, protocol::kPagePowerDevice, protocol::kUsageAudibleAlarmControl);
    test_element_ = find_control_element(device_, protocol::kPagePowerDevice, protocol::kUsageTest);
    IOHIDDeviceRegisterInputReportCallback(device_, report_, sizeof report_, &HidTransport::on_report, this);
    IOHIDDeviceScheduleWithRunLoop(device_, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    // Let the run loop deliver the first input report into the element cache.
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
  }

  ~HidTransport() override {
    IOHIDDeviceUnscheduleFromRunLoop(device_, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDDeviceRegisterInputReportCallback(device_, report_, sizeof report_, nullptr, nullptr);
    IOHIDDeviceClose(device_, kIOHIDOptionsTypeNone);
    if (alarm_element_ != nullptr) CFRelease(alarm_element_);
    if (test_element_ != nullptr) CFRelease(test_element_);
    CFRelease(device_);
    IOHIDManagerUnscheduleFromRunLoop(manager_, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDManagerClose(manager_, kIOHIDOptionsTypeNone);
    CFRelease(manager_);
  }

  const DeviceInfo& info() const override { return info_; }

  Status read_status() override {
    Status status;
    status.transport = TransportKind::Hid;
    status.ok = true;

    CFArrayRef elements = IOHIDDeviceCopyMatchingElements(device_, nullptr, kIOHIDOptionsTypeNone);
    if (elements == nullptr) {
      status.ok = false;
      status.error = Error::Io;
      status.message = "IOHIDDeviceCopyMatchingElements failed";
      return status;
    }

    double input_v = 0, output_v = 0, battery_v = 0, load = 0, freq = 0, temp = 0;
    double remain = 0, full = 0, runtime = 0;
    bool got_input = false, got_output = false, got_battery_v = false;
    bool got_load = false, got_freq = false, got_temp = false;
    bool got_remain = false, got_full = false, got_runtime = false;
    int loose_voltage = 0;

    const CFIndex count = CFArrayGetCount(elements);
    for (CFIndex i = 0; i < count; ++i) {
      auto* element = static_cast<IOHIDElementRef>(const_cast<void*>(CFArrayGetValueAtIndex(elements, i)));
      const uint32_t page = IOHIDElementGetUsagePage(element);
      const uint32_t usage = IOHIDElementGetUsage(element);
      double value = 0;
      if (!read_physical(device_, element, value)) continue;

      if (page == protocol::kPagePowerDevice && usage == 0x30) {
        // Voltage. Parent collection decides input / output / battery. High
        // that 0x84/0x30 is mapped; the parent test uses usages also mapped
        // in the same function (0x1A, 0x1C, 0x12).
        if (ancestor_is(element, protocol::kPagePowerDevice, 0x1A)) {
          input_v = value;
          got_input = true;
        } else if (ancestor_is(element, protocol::kPagePowerDevice, 0x1C)) {
          output_v = value;
          got_output = true;
        } else if (ancestor_is(element, protocol::kPagePowerDevice, 0x12)) {
          battery_v = value;
          got_battery_v = true;
        } else if (loose_voltage == 0) {
          input_v = value;
          got_input = true;
          ++loose_voltage;
        } else if (loose_voltage == 1) {
          output_v = value;
          got_output = true;
          ++loose_voltage;
        }
      } else if (page == protocol::kPagePowerDevice && usage == 0x35) {
        load = value;
        got_load = true;
      } else if (page == protocol::kPagePowerDevice && usage == 0x32) {
        freq = value;
        got_freq = true;
      } else if (page == protocol::kPagePowerDevice && usage == 0x36) {
        temp = value;
        got_temp = true;
      } else if (page == protocol::kPageBattery && usage == 0x66) {
        remain = value;
        got_remain = true;
      } else if (page == protocol::kPageBattery && usage == 0x67) {
        full = value;
        got_full = true;
      } else if (page == protocol::kPageBattery && usage == 0x68) {
        runtime = value;
        got_runtime = true;
      } else if ((page == protocol::kPageBattery && usage == 0xD0) ||
                 (page == protocol::kPageVendorFf01 && usage == 0xD0)) {
        status.ac_present = value != 0.0;
      } else if ((page == protocol::kPageBattery && (usage == 0x44 || usage == 0xD1)) ||
                 (page == protocol::kPageVendorFf01 && usage == 0xD1)) {
        status.charging = value != 0.0;
      } else if ((page == protocol::kPageBattery && (usage == 0x45 || usage == 0xD2)) ||
                 (page == protocol::kPageVendorFf01 && usage == 0xD2)) {
        status.discharging = value != 0.0;
      }
    }
    CFRelease(elements);

    if (got_load) status.load_percent = load;
    if (got_freq) status.frequency_hz = freq;
    if (got_temp) status.temperature_c = temp;
    if (got_input) status.input_voltage_v = input_v;
    if (got_output) status.output_voltage_v = output_v;
    if (got_battery_v) status.battery_voltage_v = battery_v;
    if (got_runtime) status.runtime_seconds = runtime;
    if (got_remain) {
      // RemainingCapacity is a percent on most of these UPSes. If the
      // device also exposes a larger full-charge capacity, convert.
      // Medium: the HID unit is not printed in the driver.
      if (got_full && full > 0.0 && remain > 100.0) {
        status.battery_percent = (remain / full) * 100.0;
      } else {
        status.battery_percent = remain;
      }
    }
    if (!got_remain && !got_load && !got_input && !got_runtime) {
      status.ok = false;
      status.error = Error::RespNoAvailableItem;
      status.message = "HID device exposed none of the recovered status usages";
    }

    if (raw_dump_sink() != nullptr) {
      // Give the input-report callback another chance to fill the cache.
      CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
      if (last_report_len_ > 0) {
        status.raw = hex_bytes(last_report_, last_report_len_);
        dump_raw_line("hid report " + status.raw);
      } else {
        dump_raw_line("hid read_status (no input report cached)");
      }
      std::ostringstream summary;
      summary << "hid usages bat=";
      if (got_remain) summary << remain; else summary << "n/a";
      summary << " load=";
      if (got_load) summary << load; else summary << "n/a";
      summary << " in=";
      if (got_input) summary << input_v; else summary << "n/a";
      summary << " out=";
      if (got_output) summary << output_v; else summary << "n/a";
      summary << " runtime=";
      if (got_runtime) summary << runtime; else summary << "n/a";
      summary << " ac=";
      if (status.ac_present) summary << (*status.ac_present ? 1 : 0); else summary << "n/a";
      dump_raw_line(summary.str());
    }
    return status;
  }

  Error transact(const std::string&, std::string&) override { return Error::NotSupported; }

  Error set_alarm_control(int value) override {
    return write_control(alarm_element_, protocol::kUsageAudibleAlarmControl, value,
                         fallback_alarm_report_id());
  }

  Error get_alarm_control(int& value) override {
    if (alarm_element_ == nullptr) return Error::NotSupported;
    IOHIDValueRef hid_value = nullptr;
    if (IOHIDDeviceGetValue(device_, alarm_element_, &hid_value) != kIOReturnSuccess ||
        hid_value == nullptr) {
      return Error::Io;
    }
    value = static_cast<int>(IOHIDValueGetIntegerValue(hid_value));
    return Error::Ok;
  }

  Error set_test_mode(int value) override {
    return write_control(test_element_, protocol::kUsageTest, value, fallback_test_report_id());
  }

 private:
  std::optional<uint8_t> fallback_alarm_report_id() const {
    if (info_.product_id == protocol::kPidCp1500Pfclcda) return protocol::kPid0601AlarmReportId;
    return std::nullopt;
  }

  std::optional<uint8_t> fallback_test_report_id() const {
    if (info_.product_id == protocol::kPidCp1500Pfclcda) return protocol::kPid0601TestReportId;
    return std::nullopt;
  }

  Error write_control(IOHIDElementRef element, uint32_t usage, int value,
                      std::optional<uint8_t> fallback_report_id) {
    // Preferred path: element SetValue (matches HidUps / IOHIDDeviceSetValue).
    if (element != nullptr) {
      IOHIDValueRef hid_value =
          IOHIDValueCreateWithIntegerValue(kCFAllocatorDefault, element, 0 /* timestamp */, value);
      if (hid_value == nullptr) return Error::Io;
      const IOReturn set_rc = IOHIDDeviceSetValue(device_, element, hid_value);
      CFRelease(hid_value);
      if (set_rc == kIOReturnSuccess) {
        if (raw_dump_sink() != nullptr) {
          char line[96];
          std::snprintf(line, sizeof line, "hid setvalue usage=0x%04x value=%d report_id=%u",
                        usage, value, static_cast<unsigned>(IOHIDElementGetReportID(element)));
          dump_raw_line(line);
        }
        return Error::Ok;
      }
    }

    // Fallback: Feature report [report_id][value]. Report ID from the element
    // when present, else PID 0x0601 descriptor fallbacks (High for that PID).
    uint8_t report_id = 0;
    if (element != nullptr) {
      report_id = static_cast<uint8_t>(IOHIDElementGetReportID(element));
    } else if (fallback_report_id.has_value()) {
      report_id = *fallback_report_id;
    } else {
      return Error::NotSupported;
    }

    uint8_t payload[2] = {report_id, static_cast<uint8_t>(value)};
    const IOReturn report_rc =
        IOHIDDeviceSetReport(device_, kIOHIDReportTypeFeature, report_id, payload, sizeof payload);
    if (raw_dump_sink() != nullptr) {
      char line[128];
      std::snprintf(line, sizeof line,
                    "hid setreport feature id=0x%02x usage=0x%04x value=%d rc=0x%x",
                    report_id, usage, value, static_cast<unsigned>(report_rc));
      dump_raw_line(line);
    }
    return report_rc == kIOReturnSuccess ? Error::Ok : Error::Io;
  }

  static std::string hex_bytes(const uint8_t* data, CFIndex len) {
    std::string out;
    out.reserve(static_cast<std::size_t>(len) * 3);
    for (CFIndex i = 0; i < len; ++i) {
      if (i != 0) out.push_back(' ');
      char buf[8];
      std::snprintf(buf, sizeof buf, "%02x", data[i]);
      out += buf;
    }
    return out;
  }

  static void on_report(void* context, IOReturn, void*, IOHIDReportType type, uint32_t report_id,
                        uint8_t* report, CFIndex len) {
    auto* self = static_cast<HidTransport*>(context);
    if (self == nullptr || report == nullptr || len <= 0) return;
    const CFIndex copy_len = len < static_cast<CFIndex>(sizeof self->last_report_)
                                 ? len
                                 : static_cast<CFIndex>(sizeof self->last_report_);
    std::memcpy(self->last_report_, report, static_cast<std::size_t>(copy_len));
    self->last_report_len_ = copy_len;
    if (raw_dump_sink() == nullptr) return;
    char header[64];
    std::snprintf(header, sizeof header, "hid report type=%u id=%u len=%ld ",
                  static_cast<unsigned>(type), report_id, static_cast<long>(len));
    dump_raw_line(std::string(header) + hex_bytes(report, len));
  }

  IOHIDManagerRef manager_;
  IOHIDDeviceRef device_;
  DeviceInfo info_;
  IOHIDElementRef alarm_element_ = nullptr;  // 0x84 / 0x5A
  IOHIDElementRef test_element_ = nullptr;   // 0x84 / 0x58
  uint8_t report_[1024] = {};
  uint8_t last_report_[1024] = {};
  CFIndex last_report_len_ = 0;
};

IOHIDManagerRef make_manager(std::string& error) {
  IOHIDManagerRef manager = IOHIDManagerCreate(kCFAllocatorDefault, kIOHIDOptionsTypeNone);
  if (manager == nullptr) {
    error = "IOHIDManagerCreate failed";
    return nullptr;
  }
  // Vendor 0x0764. High. Product id is intentionally not filtered: the
  // driver builds dictionaries for 0x0005 / 0x0501 / 0x0601 and then
  // special-cases further ids (0x051D and others) inside UsageMapping.
  int vendor = protocol::kVendorId;
  CFNumberRef vendor_num = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &vendor);
  CFMutableDictionaryRef match = CFDictionaryCreateMutable(
      kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
  CFDictionarySetValue(match, CFSTR(kIOHIDVendorIDKey), vendor_num);
  IOHIDManagerSetDeviceMatching(manager, match);
  CFRelease(match);
  CFRelease(vendor_num);
  IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
  if (IOHIDManagerOpen(manager, kIOHIDOptionsTypeNone) != kIOReturnSuccess) {
    IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    CFRelease(manager);
    error = "IOHIDManagerOpen failed";
    return nullptr;
  }
  CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
  return manager;
}

DeviceInfo info_from(IOHIDDeviceRef device) {
  DeviceInfo info;
  info.transport = TransportKind::Hid;
  info.vendor_id = static_cast<uint16_t>(cf_int(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey))));
  info.product_id = static_cast<uint16_t>(cf_int(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey))));
  info.location_id = cf_int(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDLocationIDKey)));
  info.product = cf_string(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey)));
  info.serial_number = cf_string(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDSerialNumberKey)));
  info.path = "hid:" + std::to_string(info.vendor_id) + ":" + std::to_string(info.product_id) + ":" +
              std::to_string(info.location_id);
  return info;
}

}  // namespace

std::vector<DeviceInfo> list_hid() {
  std::string error;
  IOHIDManagerRef manager = make_manager(error);
  if (manager == nullptr) return {};
  std::vector<DeviceInfo> found;
  CFSetRef devices = IOHIDManagerCopyDevices(manager);
  if (devices != nullptr) {
    const CFIndex count = CFSetGetCount(devices);
    std::vector<const void*> items(static_cast<std::size_t>(count));
    CFSetGetValues(devices, items.data());
    for (const void* item : items) {
      found.push_back(info_from(static_cast<IOHIDDeviceRef>(const_cast<void*>(item))));
    }
    CFRelease(devices);
  }
  IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
  IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
  CFRelease(manager);
  return found;
}

std::unique_ptr<Transport> open_hid(const DeviceInfo& info, std::string& error) {
  IOHIDManagerRef manager = make_manager(error);
  if (manager == nullptr) return nullptr;
  CFSetRef devices = IOHIDManagerCopyDevices(manager);
  IOHIDDeviceRef chosen = nullptr;
  if (devices != nullptr) {
    const CFIndex count = CFSetGetCount(devices);
    std::vector<const void*> items(static_cast<std::size_t>(count));
    CFSetGetValues(devices, items.data());
    for (const void* item : items) {
      auto* device = static_cast<IOHIDDeviceRef>(const_cast<void*>(item));
      if (info_from(device).path == info.path) {
        chosen = device;
        CFRetain(chosen);
        break;
      }
    }
    CFRelease(devices);
  }
  if (chosen == nullptr) {
    IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    error = "HID device is no longer present";
    return nullptr;
  }
  IOReturn opened = IOHIDDeviceOpen(chosen, kIOHIDOptionsTypeSeizeDevice);
  if (opened != kIOReturnSuccess) opened = IOHIDDeviceOpen(chosen, kIOHIDOptionsTypeNone);
  if (opened != kIOReturnSuccess) {
    CFRelease(chosen);
    IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
    CFRelease(manager);
    error = "IOHIDDeviceOpen failed";
    return nullptr;
  }
  // HidTransport retains both. Drop the references this function owns.
  std::unique_ptr<Transport> transport(new HidTransport(manager, chosen, info_from(chosen)));
  CFRelease(chosen);
  CFRelease(manager);
  return transport;
}

}  // namespace cyberpower::platform
