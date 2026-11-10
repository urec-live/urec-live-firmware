// Tuning console for the beacon, on the USB serial monitor and on a phone over Bluetooth
// (ble_console.h). Type `h` for the command list. Commands change MotionSettings live; `cfg` prints
// them as #defines for beacon_config.h. Replies go to every listener (console_out.h).
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
    // Commands this console does not know go here first (weight sensing); true = handled.
    bool (*extraCommand)(const char* cmd, const char* arg);
    void (*extraHelp)();
  };

  // One per input, so lines typed on USB and on the phone never mix.
  struct Line {
    char buf[48];
    uint8_t len = 0;
  };

  TuningConsole(MotionSettings& settings, const Hooks& hooks) : s_(settings), hooks_(hooks) {}

  // Non-blocking: consumes whatever bytes `in` has waiting and runs every complete line.
  void poll(Stream& in, Line& line);

  // Runs what `line` holds as a command, newline or not (a phone app's Send ends a line).
  void endLine(Line& line);

  bool streaming() const { return streaming_; }

 private:
  void execute(char* line);
  void printHelp();

  MotionSettings& s_;
  Hooks hooks_;
  bool streaming_ = false;
};
