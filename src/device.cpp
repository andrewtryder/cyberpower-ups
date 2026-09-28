#include "cyberpower/ups.hpp"

#include "internal/transport.hpp"

#include <cmath>
#include <thread>

namespace cyberpower {
namespace {

bool near_eq(const std::optional<double>& a, const std::optional<double>& b, double eps = 0.05) {
  if (a.has_value() != b.has_value()) return false;
  if (!a) return true;
  return std::fabs(*a - *b) <= eps;
}

bool same_opt_bool(const std::optional<bool>& a, const std::optional<bool>& b) {
  return a == b;
}

void interruptible_sleep(std::chrono::milliseconds duration, std::atomic<bool>& stop_flag) {
  auto remaining = duration;
  constexpr auto kSlice = std::chrono::milliseconds(50);
  while (remaining.count() > 0 && !stop_flag.load(std::memory_order_relaxed)) {
    const auto step = remaining < kSlice ? remaining : kSlice;
    std::this_thread::sleep_for(step);
    remaining -= step;
  }
}

}  // namespace

bool status_changed(const Status& previous, const Status& current) {
  if (previous.ok != current.ok) return true;
  if (previous.error != current.error) return true;
  if (previous.message != current.message) return true;
  if (previous.raw != current.raw) return true;
  if (!near_eq(previous.battery_percent, current.battery_percent)) return true;
  if (!near_eq(previous.input_voltage_v, current.input_voltage_v)) return true;
  if (!near_eq(previous.output_voltage_v, current.output_voltage_v)) return true;
  if (!near_eq(previous.load_percent, current.load_percent)) return true;
  if (!near_eq(previous.runtime_seconds, current.runtime_seconds, 1.0)) return true;
  if (!near_eq(previous.frequency_hz, current.frequency_hz)) return true;
  if (!near_eq(previous.temperature_c, current.temperature_c)) return true;
  if (!near_eq(previous.battery_voltage_v, current.battery_voltage_v)) return true;
  if (!same_opt_bool(previous.ac_present, current.ac_present)) return true;
  if (!same_opt_bool(previous.charging, current.charging)) return true;
  if (!same_opt_bool(previous.discharging, current.discharging)) return true;
  return false;
}

struct Ups::Impl {
  std::unique_ptr<platform::Transport> transport;
};

Ups::Ups(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

Ups::Ups(Ups&&) noexcept = default;
Ups& Ups::operator=(Ups&&) noexcept = default;
Ups::~Ups() = default;

const DeviceInfo& Ups::info() const { return impl_->transport->info(); }

Status Ups::read_status() { return impl_->transport->read_status(); }

void Ups::monitor(MonitorOptions options,
                  const std::function<void(const Status&)>& callback,
                  std::atomic<bool>& stop_flag) {
  if (!callback) return;
  if (options.interval < std::chrono::milliseconds(0)) {
    options.interval = std::chrono::milliseconds(0);
  }

  std::optional<Status> previous;
  while (!stop_flag.load(std::memory_order_relaxed)) {
    Status current = read_status();
    const bool first = !previous.has_value();
    const bool changed = first || status_changed(*previous, current);
    if (!options.only_on_change || changed) {
      callback(current);
    }
    previous = std::move(current);
    interruptible_sleep(options.interval, stop_flag);
  }
}

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
