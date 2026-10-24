// USB-serial tuning console for the beacon. Type `h` in the serial monitor for the command list.
// Commands change MotionSettings live; `cfg` prints them as #defines for beacon_config.h.
#pragma once

#include <Arduino.h>

#include "motion_settings.h"

class TuningConsole {
 public:
  struct Hooks {
    void (*printStatus)();
    void (*printConfig)();
    void (*settingsChanged)(bool needsRecalibration);
    void (*recalibrate)();
    void (*resetCount)();
  };

  TuningConsole(MotionSettings& settings, const Hooks& hooks) : s_(settings), hooks_(hooks) {}

  // Non-blocking: consumes whatever bytes are waiting and runs complete lines.
  void poll();

  bool streaming() const { return streaming_; }

 private:
  void execute(char* line);
  void printHelp();

  MotionSettings& s_;
  Hooks hooks_;
  char buf_[48];
  uint8_t len_ = 0;
  bool streaming_ = false;
};
