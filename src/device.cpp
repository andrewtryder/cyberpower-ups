#include "cyberpower/ups.hpp"

#include "internal/transport.hpp"

namespace cyberpower {

struct Ups::Impl {
  std::unique_ptr<platform::Transport> transport;
};

Ups::Ups(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Ups::Ups(Ups&&) noexcept = default;
Ups& Ups::operator=(Ups&&) noexcept = default;
Ups::~Ups() = default;

const DeviceInfo& Ups::info() const { return impl_->transport->info(); }

Status Ups::read_status() { return impl_->transport->read_status(); }

Error Ups::send_command(const char* command) {
  std::string unused;
  return impl_->transport->transact(command, unused);
}

Error Ups::self_test() { return send_command(protocol::cmd::kBatteryTestQuick); }

Error Ups::cancel_test() { return send_command(protocol::cmd::kCancelOrSelfTest); }

Error Ups::toggle_buzzer() { return send_command(protocol::cmd::kToggleBuzzer); }

Error Ups::read_rating(std::string& response) {
  return impl_->transport->transact(protocol::cmd::kRating, response);
}

Error Ups::cancel_schedule() { return send_command(protocol::cmd::kCancelSchedule); }

Error Ups::calibrate() { return send_command(protocol::cmd::kBatteryCalibrate); }

Error Ups::indicator_test() { return send_command(protocol::cmd::kIndicatorTest); }

Error Ups::buzzer_test() { return send_command(protocol::cmd::kBuzzerTest); }

Error Ups::transact(const std::string& command, std::string& response) {
  return impl_->transport->transact(command, response);
}

std::optional<Ups> open_device(const DeviceInfo& info, std::string* error) {
  std::string local;
  std::unique_ptr<platform::Transport> transport;
  if (info.transport == TransportKind::Hid) {
    transport = platform::open_hid(info, local);
  } else {
    transport = platform::open_serial(info, local);
  }
  if (!transport) {
    if (error) *error = local;
    return std::nullopt;
  }
  auto impl = std::make_unique<Ups::Impl>();
  impl->transport = std::move(transport);
  return Ups(std::move(impl));
}

}  // namespace cyberpower
