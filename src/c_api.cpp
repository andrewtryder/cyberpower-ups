#include "cyberpower/ups.h"

#include "cyberpower/ups.hpp"
#include "internal/c_api_internal.hpp"

#include <cstdlib>
#include <cstring>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

using cyberpower::capi::as_session;
using cyberpower::capi::dup_cstr;
using cyberpower::capi::fill_status;
using cyberpower::capi::Session;

// Runs `op` against the wrapped device and returns its Error as an int.
// A null handle maps to Error::Io.
template <typename Op>
int run_op(cp_ups* ups, Op op) {
  if (ups == nullptr) return static_cast<int>(cyberpower::Error::Io);
  return static_cast<int>(op(as_session(ups)->ups));
}

}  // namespace

extern "C" {

cp_device_list cp_ups_list(void) {
  cp_device_list list{};
  const std::vector<cyberpower::DeviceInfo> found = cyberpower::list_devices();
  if (found.empty()) return list;
  list.items = static_cast<cp_device_info*>(std::calloc(found.size(), sizeof(cp_device_info)));
  if (list.items == nullptr) return list;  // allocation failure: report an empty list
  list.count = static_cast<int>(found.size());
  for (std::size_t i = 0; i < found.size(); ++i) {
    const cyberpower::DeviceInfo& src = found[i];
    cp_device_info& dst = list.items[i];
    dst.transport = src.transport == cyberpower::TransportKind::Serial ? 1 : 0;
    dst.path = dup_cstr(src.path);
    dst.product = dup_cstr(src.product);
    dst.serial_number = dup_cstr(src.serial_number);
    dst.vendor_id = src.vendor_id;
    dst.product_id = src.product_id;
    dst.location_id = src.location_id;
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

void cp_ups_close(cp_ups* ups) { delete as_session(ups); }

void cp_ups_read_status(cp_ups* ups, cp_status* out) {
  if (out == nullptr) return;
  if (ups == nullptr) {
    std::memset(out, 0, sizeof *out);
    out->error = static_cast<int>(cyberpower::Error::Io);
    out->message = dup_cstr("null device");
    return;
  }
  fill_status(out, as_session(ups)->ups.read_status());
}

void cp_ups_status_free(cp_status* status) {
  if (status == nullptr) return;
  std::free(status->message);
  std::free(status->raw);
  std::free(status->firmware_version);
  status->message = nullptr;
  status->raw = nullptr;
  status->firmware_version = nullptr;
}

int cp_ups_self_test(cp_ups* ups) {
  return run_op(ups, [](cyberpower::Ups& u) { return u.self_test(); });
}

int cp_ups_cancel_test(cp_ups* ups) {
  return run_op(ups, [](cyberpower::Ups& u) { return u.cancel_test(); });
}

int cp_ups_toggle_buzzer(cp_ups* ups) {
  return run_op(ups, [](cyberpower::Ups& u) { return u.toggle_buzzer(); });
}

int cp_ups_enable_alarm(cp_ups* ups) {
  return run_op(ups, [](cyberpower::Ups& u) { return u.enable_alarm(); });
}

int cp_ups_disable_alarm(cp_ups* ups) {
  return run_op(ups, [](cyberpower::Ups& u) { return u.disable_alarm(); });
}

int cp_ups_mute_alarm(cp_ups* ups) {
  return run_op(ups, [](cyberpower::Ups& u) { return u.mute_alarm(); });
}

int cp_ups_read_rating(cp_ups* ups, char** rating_out) {
  if (ups == nullptr) return static_cast<int>(cyberpower::Error::Io);
  std::string reply;
  const cyberpower::Error error = as_session(ups)->ups.read_rating(reply);
  if (rating_out) *rating_out = dup_cstr(reply);
  return static_cast<int>(error);
}

int cp_ups_cancel_schedule(cp_ups* ups) {
  return run_op(ups, [](cyberpower::Ups& u) { return u.cancel_schedule(); });
}

int cp_ups_calibrate(cp_ups* ups) {
  return run_op(ups, [](cyberpower::Ups& u) { return u.calibrate(); });
}

int cp_ups_indicator_test(cp_ups* ups) {
  return run_op(ups, [](cyberpower::Ups& u) { return u.indicator_test(); });
}

int cp_ups_buzzer_test(cp_ups* ups) {
  return run_op(ups, [](cyberpower::Ups& u) { return u.buzzer_test(); });
}

int cp_ups_set_voltage_sensitivity(cp_ups* ups, int level) {
  return run_op(ups, [level](cyberpower::Ups& u) { return u.set_voltage_sensitivity(level); });
}

int cp_ups_set_shutdown_delay(cp_ups* ups, int seconds) {
  return run_op(ups, [seconds](cyberpower::Ups& u) { return u.set_shutdown_delay(seconds); });
}

int cp_ups_set_restore_delay(cp_ups* ups, int seconds) {
  return run_op(ups, [seconds](cyberpower::Ups& u) { return u.set_restore_delay(seconds); });
}

int cp_ups_transact(cp_ups* ups, const char* command, char** response) {
  if (ups == nullptr || command == nullptr) return static_cast<int>(cyberpower::Error::Io);
  std::string reply;
  const cyberpower::Error error = as_session(ups)->ups.transact(command, reply);
  if (response) *response = dup_cstr(reply);
  return static_cast<int>(error);
}

}  // extern "C"
