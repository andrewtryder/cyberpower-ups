#include "internal/transport.hpp"
#include "internal/hid_mapping.hpp"
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

uint16_t read_le16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (static_cast<uint16_t>(p[1]) << 8));
}

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
    uint8_t feat_0b[4] = {0x0B, 0, 0, 0};
    if (get_feature(feat_0b, 2)) {
      const uint8_t flags = feat_0b[1];
      HidElement ac_elem;
      ac_elem.page = protocol::kPageBattery;
      ac_elem.usage = 0xD0;
      map_element(ac_elem, (flags & 0x01) ? 1.0 : 0.0, readings, status);

      HidElement chg_elem;
      chg_elem.page = protocol::kPageBattery;
      chg_elem.usage = 0x44;
      map_element(chg_elem, (flags & 0x02) ? 1.0 : 0.0, readings, status);

      HidElement dis_elem;
      dis_elem.page = protocol::kPageBattery;
      dis_elem.usage = 0x45;
      map_element(dis_elem, (flags & 0x04) ? 1.0 : 0.0, readings, status);
    }

    // 2. Report 0x08 (Remaining capacity + runtime)
    uint8_t feat_08[8] = {0x08, 0, 0, 0, 0, 0, 0, 0};
    if (get_feature(feat_08, 4)) {
      HidElement rem_elem;
      rem_elem.page = protocol::kPageBattery;
      rem_elem.usage = protocol::kUsageDischarging; // 0x66 legacy / percent
      rem_elem.logical_max = 100;
      map_element(rem_elem, static_cast<double>(feat_08[1]), readings, status);

      HidElement rt_elem;
      rt_elem.page = protocol::kPageBattery;
      rt_elem.usage = protocol::kUsageCharging; // 0x68 legacy runtime
      rt_elem.logical_max = 65535;
      const uint16_t rt_val = read_le16(feat_08 + 2);
      map_element(rt_elem, static_cast<double>(rt_val), readings, status);
    }

    // 3. Report 0x0F (Input Voltage)
    uint8_t feat_0f[4] = {0x0F, 0, 0, 0};
    if (get_feature(feat_0f, 3)) {
      HidElement in_elem;
      in_elem.page = protocol::kPagePowerDevice;
      in_elem.usage = protocol::kUsageVoltage;
      in_elem.ancestors = {{protocol::kPagePowerDevice, 0x1A}};
      const uint16_t val = read_le16(feat_0f + 1);
      map_element(in_elem, static_cast<double>(val), readings, status);
    }

    // 4. Report 0x12 (Output Voltage)
    uint8_t feat_12[4] = {0x12, 0, 0, 0};
    if (get_feature(feat_12, 3)) {
      HidElement out_elem;
      out_elem.page = protocol::kPagePowerDevice;
      out_elem.usage = protocol::kUsageVoltage;
      out_elem.ancestors = {{protocol::kPagePowerDevice, 0x1C}};
      const uint16_t val = read_le16(feat_12 + 1);
      map_element(out_elem, static_cast<double>(val), readings, status);
    }

    // 5. Report 0x13 (Load percent)
    uint8_t feat_13[4] = {0x13, 0, 0, 0};
    if (get_feature(feat_13, 2)) {
      HidElement load_elem;
      load_elem.page = protocol::kPagePowerDevice;
      load_elem.usage = 0x35;
      map_element(load_elem, static_cast<double>(feat_13[1]), readings, status);
    }

    // 6. Report 0x0A (Battery Voltage)
    uint8_t feat_0a[4] = {0x0A, 0, 0, 0};
    if (get_feature(feat_0a, 3)) {
      HidElement bat_elem;
      bat_elem.page = protocol::kPagePowerDevice;
      bat_elem.usage = protocol::kUsageVoltage;
      bat_elem.ancestors = {{protocol::kPagePowerDevice, 0x12}};
      const uint16_t val = read_le16(feat_0a + 1);
      map_element(bat_elem, static_cast<double>(val) / 10.0, readings, status);
    }

    // 7. Report 0x07 (Full charge capacity + cycle count + need replacement)
    uint8_t feat_07[8] = {0x07, 0, 0, 0, 0, 0, 0, 0};
    if (get_feature(feat_07, 6)) {
      HidElement cycle_elem;
      cycle_elem.page = protocol::kPageBattery;
      cycle_elem.usage = protocol::kUsageCycleCount;
      map_element(cycle_elem, static_cast<double>(feat_07[3]), readings, status);

      HidElement need_elem;
      need_elem.page = protocol::kPageBattery;
      need_elem.usage = protocol::kUsageNeedReplacement;
      map_element(need_elem, feat_07[4] ? 1.0 : 0.0, readings, status);
    }

    // 8. Vendor 0xFF86 Sensitivity (Report 0x02 on PID 0x0601)
    uint8_t feat_sens[4] = {0x02, 0, 0, 0};
    if (get_feature(feat_sens, 2)) {
      HidElement sens_elem;
      sens_elem.page = protocol::kPageVendorFf86;
      sens_elem.usage = protocol::kUsageVendorSensitivityRead;
      if (feat_sens[1] >= 1 && feat_sens[1] <= 3) {
        map_element(sens_elem, static_cast<double>(feat_sens[1]), readings, status);
      }
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
    uint8_t payload[2] = {protocol::kPid0601AlarmReportId, static_cast<uint8_t>(value)};
    return set_feature(payload, sizeof payload) ? Error::Ok : Error::Io;
  }

  Error get_alarm_control(int& value) override {
    uint8_t payload[2] = {protocol::kPid0601AlarmReportId, 0};
    if (!get_feature(payload, sizeof payload)) return Error::Io;
    value = payload[1];
    return Error::Ok;
  }

  Error set_test_mode(int value) override {
    uint8_t payload[2] = {protocol::kPid0601TestReportId, static_cast<uint8_t>(value)};
    return set_feature(payload, sizeof payload) ? Error::Ok : Error::Io;
  }

  Error set_voltage_sensitivity(int level) override {
    uint8_t payload[2] = {0x02, static_cast<uint8_t>(level)};
    return set_feature(payload, sizeof payload) ? Error::Ok : Error::Io;
  }

  Error set_shutdown_delay(int seconds) override {
    uint8_t payload[3] = {0x04, static_cast<uint8_t>(seconds & 0xFF),
                          static_cast<uint8_t>((seconds >> 8) & 0xFF)};
    return set_feature(payload, sizeof payload) ? Error::Ok : Error::Io;
  }

  Error set_restore_delay(int seconds) override {
    uint8_t payload[3] = {0x05, static_cast<uint8_t>(seconds & 0xFF),
                          static_cast<uint8_t>((seconds >> 8) & 0xFF)};
    return set_feature(payload, sizeof payload) ? Error::Ok : Error::Io;
  }

 private:
  bool get_feature(uint8_t* buf, std::size_t len) {
    if (fd_ < 0 || buf == nullptr || len == 0) return false;
    const int rc = ::ioctl(fd_, HIDIOCGFEATURE(len), buf);
    if (raw_dump_sink() != nullptr && rc >= 0) {
      char line[96];
      std::snprintf(line, sizeof line, "hidraw get_feature id=0x%02x len=%zu ok", buf[0], len);
      dump_raw_line(line);
    }
    return rc >= 0;
  }

  bool set_feature(const uint8_t* buf, std::size_t len) {
    if (fd_ < 0 || buf == nullptr || len == 0) return false;
    const int rc = ::ioctl(fd_, HIDIOCSFEATURE(len), buf);
    if (raw_dump_sink() != nullptr) {
      char line[96];
      std::snprintf(line, sizeof line, "hidraw set_feature id=0x%02x len=%zu rc=%d", buf[0], len, rc);
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
