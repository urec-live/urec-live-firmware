// Short iBeacon advertising bursts through the Bluefruit (SoftDevice S140) stack.
// The radio is off between bursts; each burst sends a fixed number of advertising events.
#pragma once

#include <stdint.h>

#include "gym_protocol.h"

class IBeaconAdvertiser {
 public:
  // Starts the SoftDevice. Non-connectable, non-scannable, so there is no scan response to wait on.
  bool begin(int8_t txPowerDbm, uint16_t intervalMs);

  // Blocks for about (events - 1) * interval + interval / 2 while `events` advertising events go out.
  bool burst(const gym::BeaconFrame& frame, uint8_t events);

 private:
  uint16_t intervalMs_ = 100;
};
