#include "tuning_console.h"

#include <stdlib.h>
#include <string.h>

namespace {
bool parseNumber(const char* text, long lo, long hi, long* out) {
  if (text == nullptr || *text == '\0') return false;
  char* end = nullptr;
  const long v = strtol(text, &end, 10);
  if (*end != '\0' || v < lo || v > hi) return false;
  *out = v;
  return true;
}
}  // namespace

void TuningConsole::poll() {
  while (Serial.available() > 0) {
    const int c = Serial.read();
    if (c < 0) break;
    if (c == '\r' || c == '\n') {
      if (len_ > 0) {
        buf_[len_] = '\0';
        len_ = 0;
        execute(buf_);
      }
    } else if (len_ < sizeof(buf_) - 1) {
      buf_[len_++] = static_cast<char>(c);
    }
  }
}

void TuningConsole::printHelp() {
  Serial.println(F("commands:"));
  Serial.println(F("  s                    status"));
  Serial.println(F("  st                   toggle Teleplot stream (>a >cnt >rearm >int >reps)"));
  Serial.println(F("  mode stack|paper|lever   load that mode's preset"));
  Serial.println(F("  dl <mg> / dh <mg>    count / re-arm delta"));
  Serial.println(F("  durl <n> / durh <n>  count / re-arm duration in samples (20 ms)"));
  Serial.println(F("  hold <ms>            hold-off after the re-arm event"));
  Serial.println(F("  minint <ms>          minimum time between counted reps"));
  Serial.println(F("  axis x|y|z|auto      motion axis"));
  Serial.println(F("  cal                  recalibrate (keep the beacon still)"));
  Serial.println(F("  reset                end the current set"));
  Serial.println(F("  cfg                  print settings as #defines for beacon_config.h"));
}

void TuningConsole::execute(char* line) {
  char* cmd = strtok(line, " \t");
  char* arg = strtok(nullptr, " \t");
  if (cmd == nullptr) return;
  long v = 0;

  if (strcmp(cmd, "h") == 0 || strcmp(cmd, "help") == 0) {
    printHelp();
  } else if (strcmp(cmd, "s") == 0) {
    hooks_.printStatus();
  } else if (strcmp(cmd, "st") == 0) {
    streaming_ = !streaming_;
    if (!streaming_) Serial.println(F("stream off"));
  } else if (strcmp(cmd, "mode") == 0) {
    MotionMode m;
    if (arg == nullptr || !parseMode(arg, &m)) {
      Serial.println(F("usage: mode stack|paper|lever"));
      return;
    }
    s_ = presetFor(m, s_.axis);
    hooks_.settingsChanged(true);
  } else if (strcmp(cmd, "axis") == 0) {
    uint8_t a;
    if (arg == nullptr || !parseAxis(arg, &a)) {
      Serial.println(F("usage: axis x|y|z|auto"));
      return;
    }
    s_.axis = a;
    hooks_.settingsChanged(true);
  } else if (strcmp(cmd, "dl") == 0 && parseNumber(arg, 16, 2000, &v)) {
    s_.countDeltaMg = static_cast<uint16_t>(v);
    hooks_.settingsChanged(false);
  } else if (strcmp(cmd, "dh") == 0 && parseNumber(arg, 16, 2000, &v)) {
    s_.rearmDeltaMg = static_cast<uint16_t>(v);
    hooks_.settingsChanged(false);
  } else if (strcmp(cmd, "durl") == 0 && parseNumber(arg, 0, 127, &v)) {
    s_.countDuration = static_cast<uint8_t>(v);
    hooks_.settingsChanged(false);
  } else if (strcmp(cmd, "durh") == 0 && parseNumber(arg, 0, 127, &v)) {
    s_.rearmDuration = static_cast<uint8_t>(v);
    hooks_.settingsChanged(false);
  } else if (strcmp(cmd, "hold") == 0 && parseNumber(arg, 0, 5000, &v)) {
    s_.holdoffMs = static_cast<uint32_t>(v);
    hooks_.settingsChanged(false);
  } else if (strcmp(cmd, "minint") == 0 && parseNumber(arg, 0, 5000, &v)) {
    s_.minRepIntervalMs = static_cast<uint32_t>(v);
    hooks_.settingsChanged(false);
  } else if (strcmp(cmd, "cal") == 0) {
    hooks_.recalibrate();
  } else if (strcmp(cmd, "reset") == 0) {
    hooks_.resetCount();
  } else if (strcmp(cmd, "cfg") == 0) {
    hooks_.printConfig();
  } else {
    Serial.println(F("? (h for help)"));
  }
}
