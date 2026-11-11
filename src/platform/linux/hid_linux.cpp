#include "internal/transport.hpp"
#include "internal/hid_mapping.hpp"
#include "internal/hid_linux_decode.hpp"
#include "cyberpower/protocol.hpp"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace cyberpower::platform {
namespace {

class LinuxHidTransport : public Transport {
 public:
  LinuxHidTransport(int fd, DeviceInfo info) : fd_(fd), info_(std::move(info)) {}

  ~LinuxHidTransport() override {
    if (fd_ >= 0) ::close(fd_);
  }

  const DeviceInfo& info() const override { return info_; }

  Status read_status() override {
    Status status;
    status.transport = TransportKind::Hid;
    status.ok = true;

    // Safety check: only verified profiles are decoded with fixed report IDs.
    if (!is_verified_hid_profile(info_.vendor_id, info_.product_id)) {
      status.ok = false;
      status.error = Error::NotSupported;
      status.message = "Device model is unverified for fixed HID report layout (PID " +
                       std::to_string(info_.product_id) + ")";
      return status;
    }

    Readings readings;

    // Drain any pending input reports first.
    uint8_t in_buf[256];
    ssize_t n = ::read(fd_, in_buf, sizeof in_buf);
    if (n > 0) {
      status.raw = hex_bytes(in_buf, n);
      if (raw_dump_sink() != nullptr) {
        dump_raw_line("hid input report " + status.raw);
      }
    }

    // 1. Report 0x0B (ACPresent, Charging, Discharging)
    uint8_t feat_0b[2];
    if (get_feature(0x0B, feat_0b, sizeof feat_0b)) {
      decode_cp1500_report_0b(feat_0b, sizeof feat_0b, readings, status);
    }

    // 2. Report 0x08 (Remaining capacity + runtime)
    uint8_t feat_08[4];
    if (get_feature(0x08, feat_08, sizeof feat_08)) {
      decode_cp1500_report_08(feat_08, sizeof feat_08, readings, status);
    }

    // 3. Report 0x0F (Input Voltage)
    uint8_t feat_0f[3];
    if (get_feature(0x0F, feat_0f, sizeof feat_0f)) {
      decode_cp1500_report_0f(feat_0f, sizeof feat_0f, readings, status);
    }

    // 4. Report 0x12 (Output Voltage)
    uint8_t feat_12[3];
    if (get_feature(0x12, feat_12, sizeof feat_12)) {
      decode_cp1500_report_12(feat_12, sizeof feat_12, readings, status);
    }

    // 5. Report 0x13 (Load percent)
    uint8_t feat_13[2];
    if (get_feature(0x13, feat_13, sizeof feat_13)) {
      decode_cp1500_report_13(feat_13, sizeof feat_13, readings, status);
    }

    // 6. Report 0x0A (Battery Voltage)
    uint8_t feat_0a[3];
    if (get_feature(0x0A, feat_0a, sizeof feat_0a)) {
      decode_cp1500_report_0a(feat_0a, sizeof feat_0a, readings, status);
    }

    // 7. Report 0x07 (Capacity details + cycle count)
    uint8_t feat_07[7];
    if (get_feature(0x07, feat_07, sizeof feat_07)) {
      decode_cp1500_report_07(feat_07, sizeof feat_07, readings, status);
    }

    // 8. Vendor 0xFF86 Sensitivity (Report 0x02 on PID 0x0601)
    uint8_t feat_sens[2];
    if (get_feature(0x02, feat_sens, sizeof feat_sens)) {
      decode_cp1500_report_02(feat_sens, sizeof feat_sens, readings, status);
    }

    apply_readings(readings, status);

    if (!readings.remain && !readings.load && !readings.input_v && !readings.runtime) {
      status.ok = false;
      status.error = Error::RespNoAvailableItem;
      status.message = "Linux hidraw exposed none of the recovered status usages";
    }

    if (raw_dump_sink() != nullptr) {
      dump_raw_line(summarize(readings, status));
    }
    return status;
  }

  Error transact(const std::string&, std::string&) override {
    return Error::NotSupported;
  }

  Error set_alarm_control(int value) override {
    if (!is_verified_hid_profile(info_.vendor_id, info_.product_id)) {
      return Error::NotSupported;
    }
    if (!is_valid_cp1500_alarm_control(value)) {
      return Error::ReqParamOutOfRange;
    }
    uint8_t payload[2] = {protocol::kPid0601AlarmReportId, static_cast<uint8_t>(value)};
    return set_feature(payload, sizeof payload) ? Error::Ok : Error::Io;
  }

  Error get_alarm_control(int& value) override {
    if (!is_verified_hid_profile(info_.vendor_id, info_.product_id)) {
      return Error::NotSupported;
    }
    uint8_t payload[2];
    if (!get_feature(protocol::kPid0601AlarmReportId, payload, sizeof payload)) {
      return Error::Io;
    }
    value = payload[1];
    return Error::Ok;
  }

  Error set_test_mode(int value) override {
    if (!is_verified_hid_profile(info_.vendor_id, info_.product_id)) {
      return Error::NotSupported;
    }
    if (!is_valid_cp1500_test_mode(value)) {
      return Error::ReqParamOutOfRange;
    }
    uint8_t payload[2] = {protocol::kPid0601TestReportId, static_cast<uint8_t>(value)};
    return set_feature(payload, sizeof payload) ? Error::Ok : Error::Io;
  }

  Error set_voltage_sensitivity(int /*level*/) override {
    // Unverified vendor configuration writes are rejected for safety.
    return Error::NotSupported;
  }

  Error set_shutdown_delay(int /*seconds*/) override {
    // Unverified vendor configuration writes are rejected for safety.
    return Error::NotSupported;
  }

  Error set_restore_delay(int /*seconds*/) override {
    // Unverified vendor configuration writes are rejected for safety.
    return Error::NotSupported;
  }

 private:
  bool get_feature(uint8_t report_id, uint8_t* buf, std::size_t len) {
    if (fd_ < 0 || buf == nullptr || len == 0) return false;
    std::memset(buf, 0, len);
    buf[0] = report_id;
    const int rc = ::ioctl(fd_, HIDIOCGFEATURE(len), buf);
    const bool valid = validate_feature_report(buf, rc, report_id, len);
    if (raw_dump_sink() != nullptr) {
      char line[96];
      std::snprintf(line, sizeof line, "hidraw get_feature id=0x%02x len=%zu %s (rc=%d)",
                    report_id, len, valid ? "ok" : "failed", rc);
      dump_raw_line(line);
    }
    return valid;
  }

  bool set_feature(const uint8_t* buf, std::size_t len) {
    if (fd_ < 0 || buf == nullptr || len == 0) return false;
    const int rc = ::ioctl(fd_, HIDIOCSFEATURE(len), const_cast<uint8_t*>(buf));
    if (raw_dump_sink() != nullptr) {
      char line[96];
      std::snprintf(line, sizeof line, "hidraw set_feature id=0x%02x len=%zu %s (rc=%d)",
                    buf[0], len, rc >= 0 ? "ok" : "failed", rc);
      dump_raw_line(line);
    }
    return rc >= 0;
  }

  static std::string hex_bytes(const uint8_t* data, std::size_t len) {
    std::string out;
    out.reserve(len * 3);
    for (std::size_t i = 0; i < len; ++i) {
      if (i != 0) out.push_back(' ');
      char buf[8];
      std::snprintf(buf, sizeof buf, "%02x", data[i]);
      out += buf;
    }
    return out;
  }

  int fd_ = -1;
  DeviceInfo info_;
};

}  // namespace

std::vector<DeviceInfo> list_hid() {
  std::vector<DeviceInfo> found;
  DIR* dir = ::opendir("/dev");
  if (dir == nullptr) return found;

  while (dirent* ent = ::readdir(dir)) {
    const std::string name = ent->d_name;
    if (name.compare(0, 6, "hidraw") != 0) continue;
    const std::string dev_path = "/dev/" + name;

    const int fd = ::open(dev_path.c_str(), O_RDONLY | O_NONBLOCK);
    if (fd < 0) continue;

    struct hidraw_devinfo raw_info{};
    if (::ioctl(fd, HIDIOCGRAWINFO, &raw_info) >= 0) {
      if (static_cast<uint16_t>(raw_info.vendor) == protocol::kVendorId) {
        DeviceInfo info;
        info.transport = TransportKind::Hid;
        info.path = dev_path;
        info.vendor_id = static_cast<uint16_t>(raw_info.vendor);
        info.product_id = static_cast<uint16_t>(raw_info.product);

        char buf[256]{};
        if (::ioctl(fd, HIDIOCGRAWNAME(sizeof buf), buf) >= 0) {
          info.product = buf;
        }
        if (::ioctl(fd, HIDIOCGRAWUNIQ(sizeof buf), buf) >= 0) {
          info.serial_number = buf;
        }
        found.push_back(std::move(info));
      }
    }
    ::close(fd);
  }
  ::closedir(dir);
  return found;
}

std::unique_ptr<Transport> open_hid(const DeviceInfo& info, std::string& error) {
  int fd = ::open(info.path.c_str(), O_RDWR | O_NONBLOCK);
  if (fd < 0) {
    // If opening read-write failed, try read-only as fallback.
    fd = ::open(info.path.c_str(), O_RDONLY | O_NONBLOCK);
  }
  if (fd < 0) {
    if (errno == EACCES) {
      error = "Permission denied opening " + info.path +
              " (ensure user has read/write permission or udev rule is installed)";
    } else {
      error = "Failed to open " + info.path + ": " + std::string(std::strerror(errno));
    }
    return nullptr;
  }
  return std::make_unique<LinuxHidTransport>(fd, info);
}

}  // namespace cyberpower::platform
