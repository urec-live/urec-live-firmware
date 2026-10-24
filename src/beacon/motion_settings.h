// Rep-detection settings: which interrupt the LIS3DH raises for the count phase and the re-arm
// phase, derived from the selected mode and the calibrated resting reading of the motion axis.
// See docs/rep-counting-walkthrough.md, "Rep physics".
#pragma once

#include <stdint.h>

enum class MotionMode : uint8_t {
  Stack,  // beacon on the weight stack: count when |a| dips (top of the rep), re-arm when it rises
  Paper,  // single trigger like the paper: count when |a| dips, re-arm once it recovers
  Lever,  // beacon on a lever arm (leg curl/extension): count on tilt, re-arm when level again
};

const uint8_t kAxisX = 0;
const uint8_t kAxisY = 1;
const uint8_t kAxisZ = 2;
const uint8_t kAxisAuto = 3;  // calibration picks the axis that carries the most gravity

struct MotionSettings {
  MotionMode mode;
  uint8_t axis;               // kAxisX/Y/Z/Auto
  uint16_t countDeltaMg;      // count threshold distance from the resting reading
  uint16_t rearmDeltaMg;      // re-arm threshold distance (meaning depends on the mode)
  uint8_t countDuration;      // samples (20 ms each at 50 Hz) the count condition must hold
  uint8_t rearmDuration;      // samples the re-arm condition must hold
  uint32_t holdoffMs;         // quiet time after the re-arm event
  uint32_t minRepIntervalMs;  // count events closer than this to the last rep are ignored
};

// Result of measuring the beacon at rest.
struct Calibration {
  bool valid;          // measured, and the resting reading suits the mode
  bool measured;       // a still resting reading was taken (meanMg/restMg are meaningful)
  uint8_t axis;        // resolved axis (never kAxisAuto)
  int16_t meanMg[3];   // signed resting reading of each axis
  uint16_t restMg;     // |meanMg[axis]|: the static value thresholds are relative to
};

// One interrupt condition: |a_axis| above (high) or below (!high) thresholdMg for `duration`.
struct PhaseSpec {
  bool valid;
  bool high;
  uint16_t thresholdMg;
  uint8_t duration;
};

MotionSettings settingsFromConfig();                     // values from beacon_config.h
MotionSettings presetFor(MotionMode mode, uint8_t axis);  // recommended values per mode

const char* modeName(MotionMode mode);
const char* modeConfigName(MotionMode mode);  // "MotionMode::Stack" for printed #defines
bool parseMode(const char* text, MotionMode* out);
const char* axisName(uint8_t axis);
bool parseAxis(const char* text, uint8_t* out);

// Resolves kAxisAuto from the resting means.
uint8_t chooseAxis(const MotionSettings& s, const int16_t meanMg[3]);

// Returns nullptr when the resting reading is usable for this mode, else a short reason.
const char* checkRest(const MotionSettings& s, uint16_t restMg);

PhaseSpec countSpec(const MotionSettings& s, const Calibration& cal);
PhaseSpec rearmSpec(const MotionSettings& s, const Calibration& cal);
