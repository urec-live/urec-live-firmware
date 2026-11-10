#include "tuning_console.h"

#include <stdlib.h>
#include <string.h>

#include "console_out.h"

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

void TuningConsole::poll(Stream& in, Line& line) {
  while (in.available() > 0) {
    const int c = in.read();
    if (c < 0) break;
    if (c == '\r' || c == '\n') {
      endLine(line);
    } else if (line.len < sizeof(line.buf) - 1) {
      line.buf[line.len++] = static_cast<char>(c);
    }
  }
}

void TuningConsole::endLine(Line& line) {
  if (line.len == 0) return;
  line.buf[line.len] = '\0';
  line.len = 0;
  execute(line.buf);
}

void TuningConsole::printHelp() {
  Console.println(F("commands:"));
  Console.println(F("  s                    status"));
  Console.println(F("  st                   toggle Teleplot stream (>a >cnt >rearm >int >reps, >bx >by >bz)"));
  Console.println(F("  mode stack|paper|lever   load that mode's preset"));
  Console.println(F("  dl <mg> / dh <mg>    count / re-arm delta"));
  Console.println(F("  durl <n> / durh <n>  count / re-arm duration in samples (20 ms)"));
  Console.println(F("  hold <ms>            hold-off after the re-arm event"));
  Console.println(F("  minint <ms>          minimum time between counted reps"));
  Console.println(F("  axis x|y|z|auto      motion axis"));
  Console.println(F("  cal                  recalibrate (keep the beacon still)"));
  Console.println(F("  reset                end the current set"));
  Console.println(F("  cfg                  print settings as #defines for beacon_config.h"));
  if (hooks_.extraHelp != nullptr) hooks_.extraHelp();
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
    if (!streaming_) {
      Console.println(F("stream off"));
    } else if (!Serial) {
      Console.println(F("stream on: its lines go to the USB console only"));
    }
  } else if (strcmp(cmd, "mode") == 0) {
    MotionMode m;
    if (arg == nullptr || !parseMode(arg, &m)) {
      Console.println(F("usage: mode stack|paper|lever"));
      return;
    }
    s_ = presetFor(m, s_.axis);
    hooks_.settingsChanged(true);
  } else if (strcmp(cmd, "axis") == 0) {
    uint8_t a;
    if (arg == nullptr || !parseAxis(arg, &a)) {
      Console.println(F("usage: axis x|y|z|auto"));
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
  } else if (hooks_.extraCommand != nullptr && hooks_.extraCommand(cmd, arg)) {
    // handled
  } else {
    Console.println(F("? (h for help)"));
  }
}
