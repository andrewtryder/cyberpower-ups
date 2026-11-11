#include "internal/transport.hpp"

#include "cyberpower/protocol.hpp"
#include "internal/hid_mapping.hpp"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/hid/IOHIDManager.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
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

bool read_physical(IOHIDDeviceRef device, IOHIDElementRef element, double& out) {
  IOHIDValueRef value = nullptr;
  if (IOHIDDeviceGetValue(device, element, &value) != kIOReturnSuccess || value == nullptr)
    return false;
  out = IOHIDValueGetScaledValue(value, kIOHIDValueScaleTypePhysical);
  const CFIndex logical = IOHIDValueGetIntegerValue(value);
  // Some CyberPower reports leave the unit exponent at 0, so the physical
  // scale collapses to 0 while the logical value is the real reading.
  // Medium: fall back only in that case.
  if (out == 0.0 && logical != 0) out = static_cast<double>(logical);
  return true;
}

HidElement to_hid_element(IOHIDElementRef element) {
  HidElement desc;
  desc.page = IOHIDElementGetUsagePage(element);
  desc.usage = IOHIDElementGetUsage(element);
  const IOHIDElementType type = IOHIDElementGetType(element);
  if (type == kIOHIDElementTypeInput_Misc ||
      type == kIOHIDElementTypeInput_Button ||
      type == kIOHIDElementTypeInput_Axis ||
      type == kIOHIDElementTypeInput_ScanCodes) {
    desc.type = HidElementType::Input;
  } else if (type == kIOHIDElementTypeFeature) {
    desc.type = HidElementType::Feature;
  } else if (type == kIOHIDElementTypeOutput) {
    desc.type = HidElementType::Output;
  } else {
    desc.type = HidElementType::Other;
  }
  desc.logical_min = static_cast<int>(IOHIDElementGetLogicalMin(element));
  desc.logical_max = static_cast<int>(IOHIDElementGetLogicalMax(element));
  desc.report_id = IOHIDElementGetReportID(element);
  for (IOHIDElementRef parent = IOHIDElementGetParent(element); parent != nullptr;
       parent = IOHIDElementGetParent(parent)) {
    desc.ancestors.emplace_back(IOHIDElementGetUsagePage(parent), IOHIDElementGetUsage(parent));
  }
  return desc;
}

// Prefer Feature, then Output, for writable control elements.
IOHIDElementRef find_control_element(IOHIDDeviceRef device, uint32_t page, uint32_t usage) {
  CFArrayRef elements = IOHIDDeviceCopyMatchingElements(device, nullptr, kIOHIDOptionsTypeNone);
  if (elements == nullptr) return nullptr;

  IOHIDElementRef best = nullptr;
  IOHIDElementType best_type = kIOHIDElementTypeInput_Misc;
  const CFIndex count = CFArrayGetCount(elements);
  for (CFIndex i = 0; i < count; ++i) {
    auto* element = static_cast<IOHIDElementRef>(
        const_cast<void*>(CFArrayGetValueAtIndex(elements, i)));
    if (IOHIDElementGetUsagePage(element) != page || IOHIDElementGetUsage(element) != usage)
      continue;
    const IOHIDElementType type = IOHIDElementGetType(element);
    if (type != kIOHIDElementTypeFeature && type != kIOHIDElementTypeOutput) continue;
    if (best == nullptr ||
        (type == kIOHIDElementTypeFeature && best_type != kIOHIDElementTypeFeature)) {
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
    alarm_element_ = find_control_element(device_, protocol::kPagePowerDevice,
                                          protocol::kUsageAudibleAlarmControl);
    test_element_  = find_control_element(device_, protocol::kPagePowerDevice,
                                          protocol::kUsageTest);
    IOHIDDeviceRegisterInputReportCallback(device_, report_, sizeof report_,
                                           &HidTransport::on_report, this);
    IOHIDDeviceScheduleWithRunLoop(device_, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    // Let the run loop deliver the first input report into the element cache.
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
  }

  ~HidTransport() override {
    IOHIDDeviceUnscheduleFromRunLoop(device_, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
    IOHIDDeviceRegisterInputReportCallback(device_, report_, sizeof report_, nullptr, nullptr);
    IOHIDDeviceClose(device_, kIOHIDOptionsTypeNone);
    if (alarm_element_ != nullptr) CFRelease(alarm_element_);
    if (test_element_  != nullptr) CFRelease(test_element_);
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

    Readings readings;
    const CFIndex count = CFArrayGetCount(elements);
    for (CFIndex i = 0; i < count; ++i) {
      auto* element = static_cast<IOHIDElementRef>(
          const_cast<void*>(CFArrayGetValueAtIndex(elements, i)));
      double value = 0;
      if (!read_physical(device_, element, value)) continue;

      const HidElement desc = to_hid_element(element);
      const char* mapped_to = map_element(desc, value, readings, status);
      if (raw_dump_sink() != nullptr && mapped_to != nullptr) {
        char line[128];
        std::snprintf(line, sizeof line, "elem [%s] id=%u pg=%04x ug=%04x val=%g -> %s",
                      element_type_label(desc.type),
                      desc.report_id, desc.page,
                      desc.usage, value, mapped_to);
        dump_raw_line(line);
      }
    }
    CFRelease(elements);

    apply_readings(readings, status);

    if (!readings.remain && !readings.load && !readings.input_v && !readings.runtime) {
      status.ok = false;
      status.error = Error::RespNoAvailableItem;
      status.message = "HID device exposed none of the recovered status usages";
    }

    if (raw_dump_sink() != nullptr) dump_read_diagnostics(readings, status);
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

  // Vendor 0xFF86 config writes.
  // Prefer IOHIDDeviceSetValue via the element resolved from the descriptor.
  // No hardcoded report-ID fallback: per PROTOCOL.md, 0xFF86 report IDs must
  // come from the descriptor (IOHIDElementGetReportID); do not hardcode them.

  Error set_voltage_sensitivity(int level) override {
    // Write element: 0xFF86/0x72 (SetupVoltageSensitivity, High).
    // Encoding: 1=High, 2=Medium, 3=Low (Medium confidence — see PROTOCOL.md).
    return write_vendor_control(protocol::kUsageVendorSensitivityWrite, level);
  }

  Error set_shutdown_delay(int seconds) override {
    // 0xFF86/0x16 (High).
    return write_vendor_control(protocol::kUsageVendorShutdownDelay, seconds);
  }

  Error set_restore_delay(int seconds) override {
    // 0xFF86/0x52 (High).
    return write_vendor_control(protocol::kUsageVendorRestoreDelay, seconds);
  }

 private:
  // Writes a vendor 0xFF86 control. The report ID must come from the
  // descriptor element; there is deliberately no hardcoded fallback.
  Error write_vendor_control(uint32_t usage, int value) {
    IOHIDElementRef elem = find_control_element(device_, protocol::kPageVendorFf86, usage);
    const Error rc = write_control(elem, usage, value, std::nullopt);
    if (elem != nullptr) CFRelease(elem);
    return rc;
  }

  // Extra --dump-raw output: the last cached input report plus a one-line
  // summary of what was mapped.
  void dump_read_diagnostics(const Readings& readings, Status& status) {
    // Give the input-report callback another chance to fill the cache.
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.05, false);
    if (last_report_len_ > 0) {
      status.raw = hex_bytes(last_report_, last_report_len_);
      dump_raw_line("hid report " + status.raw);
    } else {
      dump_raw_line("hid read_status (no input report cached)");
    }
    dump_raw_line(summarize(readings, status));
  }

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
    // NOTE: vendor 0xFF86 usages pass fallback_report_id=nullopt so we do not
    // hardcode their report IDs — element is required.
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
  IOHIDDeviceRef  device_;
  DeviceInfo      info_;
  IOHIDElementRef alarm_element_ = nullptr;  // 0x84 / 0x5A
  IOHIDElementRef test_element_  = nullptr;  // 0x84 / 0x58
  uint8_t report_[1024]      = {};
  uint8_t last_report_[1024] = {};
  CFIndex last_report_len_   = 0;
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

// Unschedules, closes and releases a manager created by make_manager().
void destroy_manager(IOHIDManagerRef manager) {
  IOHIDManagerUnscheduleFromRunLoop(manager, CFRunLoopGetCurrent(), kCFRunLoopDefaultMode);
  IOHIDManagerClose(manager, kIOHIDOptionsTypeNone);
  CFRelease(manager);
}

// Snapshot of the devices currently matched by `manager` (borrowed references,
// valid while the manager is alive and the snapshot is not released).
std::vector<IOHIDDeviceRef> matched_devices(IOHIDManagerRef manager) {
  std::vector<IOHIDDeviceRef> out;
  CFSetRef devices = IOHIDManagerCopyDevices(manager);
  if (devices == nullptr) return out;
  std::vector<const void*> items(static_cast<std::size_t>(CFSetGetCount(devices)));
  CFSetGetValues(devices, items.data());
  for (const void* item : items) {
    out.push_back(static_cast<IOHIDDeviceRef>(const_cast<void*>(item)));
  }
  // The devices stay retained by the manager itself.
  CFRelease(devices);
  return out;
}

DeviceInfo info_from(IOHIDDeviceRef device) {
  DeviceInfo info;
  info.transport  = TransportKind::Hid;
  info.vendor_id  = static_cast<uint16_t>(cf_int(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDVendorIDKey))));
  info.product_id = static_cast<uint16_t>(cf_int(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductIDKey))));
  info.location_id = cf_int(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDLocationIDKey)));
  info.product     = cf_string(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDProductKey)));
  info.serial_number = cf_string(IOHIDDeviceGetProperty(device, CFSTR(kIOHIDSerialNumberKey)));
  info.path = "hid:" + std::to_string(info.vendor_id) + ":" + std::to_string(info.product_id) +
              ":" + std::to_string(info.location_id);
  return info;
}

}  // namespace

std::vector<DeviceInfo> list_hid() {
  std::string error;
  IOHIDManagerRef manager = make_manager(error);
  if (manager == nullptr) return {};
  std::vector<DeviceInfo> found;
  for (IOHIDDeviceRef device : matched_devices(manager)) found.push_back(info_from(device));
  destroy_manager(manager);
  return found;
}

std::unique_ptr<Transport> open_hid(const DeviceInfo& info, std::string& error) {
  IOHIDManagerRef manager = make_manager(error);
  if (manager == nullptr) return nullptr;
  IOHIDDeviceRef chosen = nullptr;
  for (IOHIDDeviceRef device : matched_devices(manager)) {
    if (info_from(device).path == info.path) {
      chosen = device;
      CFRetain(chosen);
      break;
    }
  }
  if (chosen == nullptr) {
    destroy_manager(manager);
    error = "HID device is no longer present";
    return nullptr;
  }
  IOReturn opened = IOHIDDeviceOpen(chosen, kIOHIDOptionsTypeSeizeDevice);
  if (opened != kIOReturnSuccess) opened = IOHIDDeviceOpen(chosen, kIOHIDOptionsTypeNone);
  if (opened != kIOReturnSuccess) {
    CFRelease(chosen);
    destroy_manager(manager);
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
