#pragma once

// Command-line parsing for cpups.

#include <string>

enum class DeviceAction {
  None,
  SelfTest,
  CancelTest,
  ToggleBuzzer,
  MuteAlarm,
  EnableAlarm,
  DisableAlarm,
  Rating,
  CancelSchedule,
  Calibrate,
  IndicatorTest,
  BuzzerTest,
  SetSensitivity,
  SetShutdownDelay,
  SetRestoreDelay,
};

struct Options {
  bool self_test_only = false;
  bool json = false;
  bool monitor = false;
  bool every_poll = false;
  bool dump_raw = false;
  int interval_ms = 2000;
  DeviceAction device_action = DeviceAction::None;
  int action_param = 0;  // numeric parameter for Set* actions
};

const char* device_action_name(DeviceAction action);

// Parses argv into `options`. Returns -1 to continue running, otherwise the
// process exit code (0 for --help, 2 for usage errors).
int parse_args(int argc, char** argv, Options& options);
