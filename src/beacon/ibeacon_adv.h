// Short iBeacon advertising bursts through the Bluefruit (SoftDevice S140) stack.
// The radio is off between bursts; each burst sends a fixed number of advertising events.
#pragma once

#include <stdint.h>

#include "gym_protocol.h"

class IBeaconAdvertiser {
 public:
  // Starts the SoftDevice. Non-connectable, non-scannable, so there is no scan response to wait on.
  bool begin(int8_t txPowerDbm, uint16_t intervalMs);

  // Starts a burst and returns at once; the caller calls stop() after burstMs(events). The burst
  // must not block: the weight recorder samples right through the top of every rep, which is
  // exactly when a rep is counted. The SoftDevice also stops by itself after 1 s.
  bool start(const gym::BeaconFrame& frame);
  void stop();

  // How long `events` advertising events take: (events - 1) * interval + interval / 2.
  uint32_t burstMs(uint8_t events) const {
    if (events < 1) events = 1;
    return static_cast<uint32_t>(events - 1) * intervalMs_ + intervalMs_ / 2;
  }

 private:
  uint16_t intervalMs_ = 100;
};
