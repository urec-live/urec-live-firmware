#include "motion_settings.h"

#include <stdlib.h>
#include <string.h>

#include "beacon_config.h"

namespace {
const uint16_t kLsbMg = 16;     // INT1_THS step at +/-2 g
const uint16_t kMaxThsMg = 2032;  // 127 * 16
const uint16_t kLeverMaxRestMg = 500;  // lever axis within ~30 degrees of horizontal at rest

PhaseSpec makeSpec(bool high, int32_t thresholdMg, uint8_t duration) {
  PhaseSpec p;
  p.high = high;
  p.duration = duration;
  p.valid = thresholdMg >= kLsbMg && thresholdMg <= kMaxThsMg;
  p.thresholdMg = p.valid ? static_cast<uint16_t>(thresholdMg) : 0;
  return p;
}
}  // namespace

MotionSettings settingsFromConfig() {
  MotionSettings s;
  s.mode = BEACON_MOTION_MODE;
  s.axis = BEACON_AXIS;
  s.countDeltaMg = BEACON_COUNT_DELTA_MG;
  s.rearmDeltaMg = BEACON_REARM_DELTA_MG;
  s.countDuration = BEACON_COUNT_DURATION;
  s.rearmDuration = BEACON_REARM_DURATION;
  s.holdoffMs = BEACON_HOLDOFF_MS;
  s.minRepIntervalMs = BEACON_MIN_REP_INTERVAL_MS;
  return s;
}

MotionSettings presetFor(MotionMode mode, uint8_t axis) {
  MotionSettings s;
  s.mode = mode;
  s.axis = axis;
  s.minRepIntervalMs = 600;
  switch (mode) {
    case MotionMode::Paper:
      s.countDeltaMg = 64;
      s.rearmDeltaMg = 48;
      s.countDuration = 4;
      s.rearmDuration = 2;
      s.holdoffMs = 0;
      break;
    case MotionMode::Lever:
      s.countDeltaMg = 350;
      s.rearmDeltaMg = 150;
      s.countDuration = 4;
      s.rearmDuration = 4;
      s.holdoffMs = 200;
      break;
    case MotionMode::Stack:
    default:
      s.countDeltaMg = 64;
      s.rearmDeltaMg = 64;
      s.countDuration = 4;
      s.rearmDuration = 2;
      s.holdoffMs = 350;
      break;
  }
  return s;
}

const char* modeName(MotionMode mode) {
  switch (mode) {
    case MotionMode::Paper: return "paper";
    case MotionMode::Lever: return "lever";
    case MotionMode::Stack:
    default: return "stack";
  }
}

const char* modeConfigName(MotionMode mode) {
  switch (mode) {
    case MotionMode::Paper: return "MotionMode::Paper";
    case MotionMode::Lever: return "MotionMode::Lever";
    case MotionMode::Stack:
    default: return "MotionMode::Stack";
  }
}

bool parseMode(const char* text, MotionMode* out) {
  if (strcmp(text, "stack") == 0) *out = MotionMode::Stack;
  else if (strcmp(text, "paper") == 0) *out = MotionMode::Paper;
  else if (strcmp(text, "lever") == 0) *out = MotionMode::Lever;
  else return false;
  return true;
}

const char* axisName(uint8_t axis) {
  switch (axis) {
    case kAxisX: return "x";
    case kAxisY: return "y";
    case kAxisZ: return "z";
    default: return "auto";
  }
}

bool parseAxis(const char* text, uint8_t* out) {
  if (strcmp(text, "x") == 0) *out = kAxisX;
  else if (strcmp(text, "y") == 0) *out = kAxisY;
  else if (strcmp(text, "z") == 0) *out = kAxisZ;
  else if (strcmp(text, "auto") == 0) *out = kAxisAuto;
  else return false;
  return true;
}

uint8_t chooseAxis(const MotionSettings& s, const int16_t meanMg[3]) {
  if (s.axis != kAxisAuto) return s.axis;
  if (s.mode == MotionMode::Lever) return kAxisX;  // no sensible automatic choice; documented
  uint8_t best = kAxisX;
  for (uint8_t a = 1; a < 3; a++) {
    if (abs(meanMg[a]) > abs(meanMg[best])) best = a;
  }
  return best;
}

const char* checkRest(const MotionSettings& s, uint16_t restMg) {
  if (s.mode == MotionMode::Lever) {
    // The lever axis must be roughly horizontal at rest so that tilting raises |a|; on a
    // vertical axis tilting lowers |a| and nothing would ever count.
    if (restMg > kLeverMaxRestMg) return "lever axis is not horizontal - choose the axis that tilts";
    if (restMg + s.countDeltaMg > kMaxThsMg) return "lever count delta is too large";
    return nullptr;
  }
  if (restMg < BEACON_MIN_GRAVITY_MG) return "motion axis is not vertical (too little gravity)";
  if (restMg <= s.countDeltaMg + kLsbMg) return "count delta is larger than the resting reading";
  return nullptr;
}

PhaseSpec countSpec(const MotionSettings& s, const Calibration& cal) {
  const int32_t rest = cal.restMg;
  switch (s.mode) {
    case MotionMode::Lever:
      return makeSpec(true, rest + s.countDeltaMg, s.countDuration);
    case MotionMode::Paper:
    case MotionMode::Stack:
    default:
      return makeSpec(false, rest - s.countDeltaMg, s.countDuration);
  }
}

PhaseSpec rearmSpec(const MotionSettings& s, const Calibration& cal) {
  const int32_t rest = cal.restMg;
  switch (s.mode) {
    case MotionMode::Paper:  // recovered above the count threshold by a hysteresis margin
      return makeSpec(true, rest - s.countDeltaMg + s.rearmDeltaMg, s.rearmDuration);
    case MotionMode::Lever:  // tilted back: rearmDelta below the count threshold
      return makeSpec(false, rest + s.countDeltaMg - s.rearmDeltaMg, s.rearmDuration);
    case MotionMode::Stack:
    default:  // the stack is pushed up again (bottom of the rep / next lift)
      return makeSpec(true, rest + s.rearmDeltaMg, s.rearmDuration);
  }
}
