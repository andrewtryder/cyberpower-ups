#pragma once

#include <signal.h>

// Pure C facade over the C++ API. Strings returned by cp_ups_list are
// owned by the library until cp_ups_list_free. Status strings are owned
// by the caller after cp_ups_status_free.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cp_device_info {
  int transport; /* 0 = HID, 1 = serial */
  const char* path;
  const char* product;
  const char* serial_number;
  uint16_t vendor_id;
  uint16_t product_id;
  int location_id;
} cp_device_info;

typedef struct cp_device_list {
  cp_device_info* items;
  int count;
} cp_device_list;

typedef struct cp_status {
  int ok;
  int error; /* cyberpower::Error numeric value */
  char* message;
  int transport;
  char* raw;
  int has_battery_percent;
  double battery_percent;
  int has_input_voltage_v;
  double input_voltage_v;
  int has_output_voltage_v;
  double output_voltage_v;
  int has_load_percent;
  double load_percent;
  int has_runtime_seconds;
  double runtime_seconds;
  int has_frequency_hz;
  double frequency_hz;
  int has_temperature_c;
  double temperature_c;
  int has_battery_voltage_v;
  double battery_voltage_v;
  int has_ac_present;
  int ac_present;
  int has_charging;
  int charging;
  int has_discharging;
  int discharging;
  char* firmware_version; /* dynamically allocated, freed in cp_ups_status_free */
  int has_cycle_count;
  double cycle_count;
  int has_need_replacement;
  int need_replacement;
  int has_voltage_sensitivity;
  int voltage_sensitivity;
  int has_shutdown_delay_s;
  double shutdown_delay_s;
  int has_restore_delay_s;
  double restore_delay_s;
} cp_status;

typedef struct cp_ups cp_ups;

cp_device_list cp_ups_list(void);
void cp_ups_list_free(cp_device_list* list);

/* Returns NULL and writes a malloc'd message to err_out (optional). */
cp_ups* cp_ups_open(const cp_device_info* info, char** err_out);
void cp_ups_close(cp_ups* ups);

void cp_ups_read_status(cp_ups* ups, cp_status* out);
void cp_ups_status_free(cp_status* status);

/* High-level commands. Return the Error numeric value.
   self_test / cancel_test / toggle_buzzer / calibrate work on HID and serial.
   enable/disable/mute are HID Feature writes (NotSupported on serial).
   indicator_test / buzzer_test / rating / cancel_schedule are serial-only. */
int cp_ups_self_test(cp_ups* ups);
int cp_ups_cancel_test(cp_ups* ups);
int cp_ups_toggle_buzzer(cp_ups* ups);
int cp_ups_enable_alarm(cp_ups* ups);
int cp_ups_disable_alarm(cp_ups* ups);
int cp_ups_mute_alarm(cp_ups* ups);
/* rating_out is malloc'd on success (caller frees). May be NULL. */
int cp_ups_read_rating(cp_ups* ups, char** rating_out);
int cp_ups_cancel_schedule(cp_ups* ups);
int cp_ups_calibrate(cp_ups* ups);
int cp_ups_indicator_test(cp_ups* ups);
int cp_ups_buzzer_test(cp_ups* ups);

/* HID-only config writes. */
int cp_ups_set_voltage_sensitivity(cp_ups* ups, int level);
int cp_ups_set_shutdown_delay(cp_ups* ups, int seconds);
int cp_ups_set_restore_delay(cp_ups* ups, int seconds);

/* Serial transact. response is malloc'd. Returns the Error numeric value. */
int cp_ups_transact(cp_ups* ups, const char* command, char** response);

/* Blocking status monitor on the calling thread (no background worker).
   Invokes callback for the first sample and then according to only_on_change.
   This legacy variant is only for a signal handler: stop_flag must be a
   volatile sig_atomic_t set non-zero by that handler. It is not safe for
   communication with another thread. The cp_status pointer is valid only
   during the callback. */
typedef void (*cp_ups_monitor_cb)(const cp_status* status, int changed, void* user_data);
void cp_ups_monitor(cp_ups* ups,
                    int interval_ms,
                    int only_on_change,
                    volatile sig_atomic_t* stop_flag,
                    cp_ups_monitor_cb callback,
                    void* user_data);

/* Thread-safe monitor stop token. Create and request may be called from
   ordinary threads. Do not destroy the token until every
   cp_ups_monitor_with_stop_token call using it has returned. Request is not
   async-signal-safe; use cp_ups_monitor's sig_atomic_t variant when stopping
   from a signal handler. */
typedef struct cp_ups_stop_token cp_ups_stop_token;
cp_ups_stop_token* cp_ups_stop_token_create(void);
void cp_ups_stop_token_request(cp_ups_stop_token* token);
void cp_ups_stop_token_destroy(cp_ups_stop_token* token);
void cp_ups_monitor_with_stop_token(cp_ups* ups,
                                    int interval_ms,
                                    int only_on_change,
                                    const cp_ups_stop_token* stop_token,
                                    cp_ups_monitor_cb callback,
                                    void* user_data);

#ifdef __cplusplus
}
#endif
