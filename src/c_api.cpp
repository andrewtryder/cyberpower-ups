#include "cyberpower/ups.h"

#include "cyberpower/ups.hpp"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

char* dup_cstr(const std::string& text) {
  char* out = static_cast<char*>(std::malloc(text.size() + 1));
  if (out == nullptr) return nullptr;
  std::memcpy(out, text.c_str(), text.size() + 1);
  return out;
}

void set_opt(int& has, double& dest, const std::optional<double>& value) {
  if (value) {
    has = 1;
    dest = *value;
  }
}

void fill_status(cp_status* out, const cyberpower::Status& status) {
  std::memset(out, 0, sizeof *out);
  out->ok = status.ok ? 1 : 0;
  out->error = static_cast<int>(status.error);
  out->message = dup_cstr(status.message);
  out->transport = status.transport == cyberpower::TransportKind::Serial ? 1 : 0;
  out->raw = dup_cstr(status.raw);
  set_opt(out->has_battery_percent, out->battery_percent, status.battery_percent);
  set_opt(out->has_input_voltage_v, out->input_voltage_v, status.input_voltage_v);
  set_opt(out->has_output_voltage_v, out->output_voltage_v, status.output_voltage_v);
  set_opt(out->has_load_percent, out->load_percent, status.load_percent);
  set_opt(out->has_runtime_seconds, out->runtime_seconds, status.runtime_seconds);
  set_opt(out->has_frequency_hz, out->frequency_hz, status.frequency_hz);
  set_opt(out->has_temperature_c, out->temperature_c, status.temperature_c);
  set_opt(out->has_battery_voltage_v, out->battery_voltage_v, status.battery_voltage_v);
  if (status.ac_present) {
    out->has_ac_present = 1;
    out->ac_present = *status.ac_present ? 1 : 0;
  }
  if (status.charging) {
    out->has_charging = 1;
    out->charging = *status.charging ? 1 : 0;
  }
  if (status.discharging) {
    out->has_discharging = 1;
    out->discharging = *status.discharging ? 1 : 0;
  }
}

struct Session {
  cyberpower::Ups ups;
  explicit Session(cyberpower::Ups opened) : ups(std::move(opened)) {}
};

}  // namespace

extern "C" {

cp_device_list cp_ups_list(void) {
  cp_device_list list{};
  const std::vector<cyberpower::DeviceInfo> found = cyberpower::list_devices();
  list.count = static_cast<int>(found.size());
  if (list.count == 0) return list;
  list.items = static_cast<cp_device_info*>(std::calloc(static_cast<std::size_t>(list.count), sizeof(cp_device_info)));
  for (int i = 0; i < list.count; ++i) {
    list.items[i].transport = found[static_cast<std::size_t>(i)].transport == cyberpower::TransportKind::Serial ? 1 : 0;
    list.items[i].path = dup_cstr(found[static_cast<std::size_t>(i)].path);
    list.items[i].product = dup_cstr(found[static_cast<std::size_t>(i)].product);
    list.items[i].serial_number = dup_cstr(found[static_cast<std::size_t>(i)].serial_number);
    list.items[i].vendor_id = found[static_cast<std::size_t>(i)].vendor_id;
    list.items[i].product_id = found[static_cast<std::size_t>(i)].product_id;
    list.items[i].location_id = found[static_cast<std::size_t>(i)].location_id;
  }
  return list;
}

void cp_ups_list_free(cp_device_list* list) {
  if (list == nullptr) return;
  for (int i = 0; i < list->count; ++i) {
    std::free(const_cast<char*>(list->items[i].path));
    std::free(const_cast<char*>(list->items[i].product));
    std::free(const_cast<char*>(list->items[i].serial_number));
  }
  std::free(list->items);
  list->items = nullptr;
  list->count = 0;
}

cp_ups* cp_ups_open(const cp_device_info* info, char** err_out) {
  if (info == nullptr) return nullptr;
  cyberpower::DeviceInfo device;
  device.transport = info->transport == 1 ? cyberpower::TransportKind::Serial : cyberpower::TransportKind::Hid;
  if (info->path) device.path = info->path;
  if (info->product) device.product = info->product;
  if (info->serial_number) device.serial_number = info->serial_number;
  device.vendor_id = info->vendor_id;
  device.product_id = info->product_id;
  device.location_id = info->location_id;
  std::string error;
  std::optional<cyberpower::Ups> opened = cyberpower::open_device(device, &error);
  if (!opened) {
    if (err_out) *err_out = dup_cstr(error);
    return nullptr;
  }
  return reinterpret_cast<cp_ups*>(new Session(std::move(*opened)));
}

void cp_ups_close(cp_ups* ups) { delete reinterpret_cast<Session*>(ups); }

void cp_ups_read_status(cp_ups* ups, cp_status* out) {
  if (out == nullptr) return;
  if (ups == nullptr) {
    std::memset(out, 0, sizeof *out);
    out->error = static_cast<int>(cyberpower::Error::Io);
    out->message = dup_cstr("null device");
    return;
  }
  fill_status(out, reinterpret_cast<Session*>(ups)->ups.read_status());
}

void cp_ups_status_free(cp_status* status) {
  if (status == nullptr) return;
  std::free(status->message);
  std::free(status->raw);
  status->message = nullptr;
  status->raw = nullptr;
}

int cp_ups_self_test(cp_ups* ups) {
  if (ups == nullptr) return static_cast<int>(cyberpower::Error::Io);
  return static_cast<int>(reinterpret_cast<Session*>(ups)->ups.self_test());
}

int cp_ups_cancel_test(cp_ups* ups) {
  if (ups == nullptr) return static_cast<int>(cyberpower::Error::Io);
  return static_cast<int>(reinterpret_cast<Session*>(ups)->ups.cancel_test());
}

int cp_ups_toggle_buzzer(cp_ups* ups) {
  if (ups == nullptr) return static_cast<int>(cyberpower::Error::Io);
  return static_cast<int>(reinterpret_cast<Session*>(ups)->ups.toggle_buzzer());
}

int cp_ups_read_rating(cp_ups* ups, char** rating_out) {
  if (ups == nullptr) return static_cast<int>(cyberpower::Error::Io);
  std::string reply;
  const cyberpower::Error error = reinterpret_cast<Session*>(ups)->ups.read_rating(reply);
  if (rating_out) *rating_out = dup_cstr(reply);
  return static_cast<int>(error);
}

int cp_ups_cancel_schedule(cp_ups* ups) {
  if (ups == nullptr) return static_cast<int>(cyberpower::Error::Io);
  return static_cast<int>(reinterpret_cast<Session*>(ups)->ups.cancel_schedule());
}

int cp_ups_calibrate(cp_ups* ups) {
  if (ups == nullptr) return static_cast<int>(cyberpower::Error::Io);
  return static_cast<int>(reinterpret_cast<Session*>(ups)->ups.calibrate());
}

int cp_ups_indicator_test(cp_ups* ups) {
  if (ups == nullptr) return static_cast<int>(cyberpower::Error::Io);
  return static_cast<int>(reinterpret_cast<Session*>(ups)->ups.indicator_test());
}

int cp_ups_buzzer_test(cp_ups* ups) {
  if (ups == nullptr) return static_cast<int>(cyberpower::Error::Io);
  return static_cast<int>(reinterpret_cast<Session*>(ups)->ups.buzzer_test());
}

int cp_ups_transact(cp_ups* ups, const char* command, char** response) {
  if (ups == nullptr || command == nullptr) return static_cast<int>(cyberpower::Error::Io);
  std::string reply;
  const cyberpower::Error error = reinterpret_cast<Session*>(ups)->ups.transact(command, reply);
  if (response) *response = dup_cstr(reply);
  return static_cast<int>(error);
}

void cp_ups_monitor(cp_ups* ups,
                    int interval_ms,
                    int only_on_change,
                    volatile int* stop_flag,
                    cp_ups_monitor_cb callback,
                    void* user_data) {
  if (ups == nullptr || callback == nullptr || stop_flag == nullptr) return;

  const auto interval = std::chrono::milliseconds(interval_ms > 0 ? interval_ms : 2000);
  const bool filter = only_on_change != 0;
  auto& device = reinterpret_cast<Session*>(ups)->ups;
  std::optional<cyberpower::Status> previous;

  while (!*stop_flag) {
    const cyberpower::Status status = device.read_status();
    const bool first = !previous.has_value();
    const bool changed = first || cyberpower::status_changed(*previous, status);
    if (!filter || changed) {
      cp_status view{};
      fill_status(&view, status);
      callback(&view, changed ? 1 : 0, user_data);
      cp_ups_status_free(&view);
    }
    previous = status;

    auto remaining = interval;
    constexpr auto kSlice = std::chrono::milliseconds(50);
    while (remaining.count() > 0 && !*stop_flag) {
      const auto step = remaining < kSlice ? remaining : kSlice;
      std::this_thread::sleep_for(step);
      remaining -= step;
    }
  }
}

}  // extern "C"
