#include "internal/transport.hpp"
#include "internal/serial_discovery.hpp"
#include "internal/serial_write.hpp"

#include "cyberpower/protocol.hpp"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include <string>
#include <cstdlib>
#include <vector>

namespace cyberpower::platform {
namespace {

speed_t darwin_speed(int baud) {
  switch (baud) {
    case 1200: return B1200;
    case 2400: return B2400;
    case 4800: return B4800;
    case 9600: return B9600;
    default: return 0;
  }
}

class SerialTransport : public Transport {
 public:
  SerialTransport(int fd, DeviceInfo info) : fd_(fd), info_(std::move(info)) {}

  ~SerialTransport() override {
    if (fd_ >= 0) ::close(fd_);
  }

  const DeviceInfo& info() const override { return info_; }

  Status read_status() override {
    Status status;
    status.transport = TransportKind::Serial;
    std::string response;
    status.error = transact(protocol::cmd::kStatus, response);
    status.raw = response;
    if (status.error != Error::Ok) {
      status.ok = false;
      status.message = error_message(status.error);
      return status;
    }
    status.frame = protocol::parse_v2e_status(response, false);
    status.error = status.frame.error;
    status.ok = status.frame.ok;
    if (!status.ok) {
      status.message = error_message(status.error);
      return status;
    }
    apply_field(status, 'B', status.battery_percent);
    apply_field(status, 'I', status.input_voltage_v);
    apply_field(status, 'O', status.output_voltage_v);
    apply_field(status, 'L', status.load_percent);
    apply_field(status, 'F', status.frequency_hz);
    apply_field(status, 'T', status.temperature_c);
    apply_field(status, 'H', status.battery_voltage_v);
    const auto runtime = status.frame.fields.find('R');
    if (runtime != status.frame.fields.end()) status.runtime_seconds = runtime->second.stored;
    return status;
  }

  Error transact(const std::string& command, std::string& response) override {
    std::string wire = command;
    if (wire.empty() || wire.back() != protocol::kFrameDelimiter) wire.push_back(protocol::kFrameDelimiter);
    dump_raw_line(std::string("serial tx ") + escape_for_dump(wire));
    const Error write_error = write_all_nonblocking(fd_, wire.data(), wire.size(), 1500);
    if (write_error != Error::Ok) return write_error;

    response.clear();
    const int timeout_ms = 1500;
    int waited = 0;
    while (waited < timeout_ms) {
      pollfd pfd{};
      pfd.fd = fd_;
      pfd.events = POLLIN;
      const int pr = ::poll(&pfd, 1, 100);
      waited += 100;
      if (pr < 0) {
        if (errno == EINTR) continue;
        return Error::Io;
      }
      if (pr == 0) continue;
      char buf[256];
      const ssize_t n = ::read(fd_, buf, sizeof buf);
      if (n < 0) {
        if (errno == EAGAIN || errno == EINTR) continue;
        return Error::Io;
      }
      if (n == 0) continue;
      response.append(buf, buf + n);
      if (response.find(protocol::kFrameDelimiter) != std::string::npos) {
        const auto cut = response.find(protocol::kFrameDelimiter);
        response.resize(cut + 1);
        dump_raw_line(std::string("serial rx ") + escape_for_dump(response));
        return Error::Ok;
      }
    }
    if (!response.empty()) {
      dump_raw_line(std::string("serial rx (incomplete) ") + escape_for_dump(response));
    }
    return response.empty() ? Error::RespEmpty : Error::Timeout;
  }

 private:
  static std::string escape_for_dump(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (unsigned char c : text) {
      if (c == '\r') {
        out += "\\r";
      } else if (c == '\n') {
        out += "\\n";
      } else if (c >= 0x20 && c < 0x7f) {
        out.push_back(static_cast<char>(c));
      } else {
        char buf[8];
        std::snprintf(buf, sizeof buf, "\\x%02x", c);
        out += buf;
      }
    }
    return out;
  }

  static void apply_field(Status& status, char tag, std::optional<double>& dest) {
    const auto it = status.frame.fields.find(tag);
    if (it == status.frame.fields.end()) return;
    // Ordinary fields are stored as nominal * 1000. Names (percent, volts,
    // hertz, celsius) are Medium; the scale is High.
    dest = protocol::nominal_from_stored(tag, it->second.stored);
  }

  int fd_ = -1;
  DeviceInfo info_;
};

}  // namespace

std::vector<DeviceInfo> list_serial() {
  std::vector<DeviceInfo> found;
  DIR* dir = ::opendir("/dev");
  if (dir == nullptr) return found;
  // Generic USB serial nodes cannot be tied to CyberPower by their filename.
  // They are opt-in, so ordinary `cpups` never sends D\\r to unrelated gear.
  const char* generic_setting = std::getenv("CPUPS_INCLUDE_GENERIC_SERIAL");
  const bool include_generic = generic_setting != nullptr && std::string(generic_setting) == "1";
  while (dirent* ent = ::readdir(dir)) {
    const std::string name = ent->d_name;
    if (!serial_port_name_is_candidate(name, include_generic)) continue;
    if (name.compare(0, 3, "cu.") != 0 && name.compare(0, 4, "tty.") != 0 &&
        name.compare(0, 6, "ttyUSB") != 0 && name.compare(0, 4, "ttyS") != 0) {
      continue;
    }
    DeviceInfo info;
    info.transport = TransportKind::Serial;
    info.path = std::string("/dev/") + name;
    info.product = name;
    found.push_back(std::move(info));
  }
  ::closedir(dir);
  return found;
}

std::unique_ptr<Transport> open_serial(const DeviceInfo& info, std::string& error) {
  const int fd = ::open(info.path.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
  if (fd < 0) {
    error = std::string("open failed: ") + strerror(errno);
    return nullptr;
  }
  termios tio{};
  if (tcgetattr(fd, &tio) != 0) {
    error = "tcgetattr failed";
    ::close(fd);
    return nullptr;
  }
  cfmakeraw(&tio);
  // Immediates from SerialCommImp::Initialize. High.
  tio.c_iflag = protocol::kTermiosIflag;
  tio.c_oflag = 0;
  tio.c_lflag = 0;
  tio.c_cflag = static_cast<tcflag_t>(protocol::kTermiosCflag);
  tio.c_cc[VMIN] = 0;
  tio.c_cc[VTIME] = 0;
  const speed_t speed = darwin_speed(protocol::kDefaultBaud);
  if (speed == 0 || cfsetispeed(&tio, speed) != 0 || cfsetospeed(&tio, speed) != 0) {
    error = "unsupported baud";
    ::close(fd);
    return nullptr;
  }
  if (tcsetattr(fd, TCSANOW, &tio) != 0) {
    error = "tcsetattr failed";
    ::close(fd);
    return nullptr;
  }
  tcflush(fd, TCIOFLUSH);
  return std::unique_ptr<Transport>(new SerialTransport(fd, info));
}

}  // namespace cyberpower::platform
