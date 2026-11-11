#include "cyberpower/ups.h"

#include "internal/c_api_internal.hpp"

#include <atomic>
#include <chrono>
#include <new>
#include <optional>
#include <thread>

struct cp_ups_stop_token {
  std::atomic<bool> requested{false};
};

namespace {

using cyberpower::capi::as_session;
using cyberpower::capi::fill_status;

template <typename Stopped>
void monitor_impl(cp_ups* ups, int interval_ms, int only_on_change, Stopped stopped,
                  cp_ups_monitor_cb callback, void* user_data) {
  if (ups == nullptr || callback == nullptr) return;
  const auto interval = std::chrono::milliseconds(interval_ms > 0 ? interval_ms : 2000);
  const bool filter = only_on_change != 0;
  auto& device = as_session(ups)->ups;
  std::optional<cyberpower::Status> previous;
  while (!stopped()) {
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
    while (remaining.count() > 0 && !stopped()) {
      const auto step = remaining < kSlice ? remaining : kSlice;
      std::this_thread::sleep_for(step);
      remaining -= step;
    }
  }
}

}  // namespace

extern "C" {

void cp_ups_monitor(cp_ups* ups,
                    int interval_ms,
                    int only_on_change,
                    volatile sig_atomic_t* stop_flag,
                    cp_ups_monitor_cb callback,
                    void* user_data) {
  if (stop_flag == nullptr) return;
  monitor_impl(ups, interval_ms, only_on_change, [stop_flag] { return *stop_flag != 0; }, callback, user_data);
}

cp_ups_stop_token* cp_ups_stop_token_create(void) { return new (std::nothrow) cp_ups_stop_token; }
void cp_ups_stop_token_request(cp_ups_stop_token* token) {
  if (token != nullptr) token->requested.store(true, std::memory_order_release);
}
void cp_ups_stop_token_destroy(cp_ups_stop_token* token) { delete token; }
void cp_ups_monitor_with_stop_token(cp_ups* ups, int interval_ms, int only_on_change,
                                    const cp_ups_stop_token* token, cp_ups_monitor_cb callback,
                                    void* user_data) {
  if (token == nullptr) return;
  monitor_impl(ups, interval_ms, only_on_change,
               [token] { return token->requested.load(std::memory_order_acquire); }, callback, user_data);
}

}  // extern "C"
