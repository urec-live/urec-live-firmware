// Weight-sensing options for beacon_config.h (docs/weight-sensing.md).
#pragma once

#include <stdint.h>

// Which magnetometer(s) the beacon uses from the LIS3DH's STEMMA QT chain.
enum class MagSensor : uint8_t {
  Auto,      // whatever answers (both, if both are fitted)
  Tmag5273,  // TI TMAG5273 3D Hall sensor, Adafruit #6489 (approach 2 in the schematic)
  Lis2mdl,   // ST LIS2MDL magnetometer, Adafruit #4488 (approach 1)
  Both,      // both, every set classified by each of them
};

// Slots: 0 = TMAG5273, 1 = LIS2MDL. Each has its own samples, taught weights and flash file.
const uint8_t kMaxMagnetometers = 2;
