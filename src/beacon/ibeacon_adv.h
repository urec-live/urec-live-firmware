// Short advertising bursts through the Bluefruit (SoftDevice S140) stack: the iBeacon rep frame,
// and the status frame that carries the battery voltage (lib/gym_core/src/gym_protocol.h).
// The radio is off between bursts; each burst sends a fixed number of advertising events.
//
// The same (single) advertising set also carries the Bluetooth console's connectable advertisement
// (ble_console.h) between bursts. A burst replaces it; loop() starts it again when the burst ends.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "gym_protocol.h"

class BLEService;

class IBeaconAdvertiser {
 public:
  // Starts the SoftDevice. Bursts are non-connectable and non-scannable, so there is no scan
  // response to wait on.
  bool begin(int8_t txPowerDbm, uint16_t intervalMs);

  // Starts a burst and returns at once; the caller calls stop() after burstMs(events). The burst
  // must not block: the weight recorder samples right through the top of every rep, which is
  // exactly when a rep is counted. The SoftDevice also stops by itself after 1 s. A new burst
  // replaces one that is still on the air, and the console advertisement.
  bool start(const gym::BeaconFrame& frame);
  bool start(const gym::StatusFrame& frame);

  // Connectable advertising for a phone, with `service` (the console's UART) in the advertisement
  // and the device name in the scan response. Runs until stop(), a burst, or a phone connects.
  bool startConsole(BLEService& service, uint16_t intervalMs);
  bool consoleOn() const { return mode_ == kConsole; }

  void stop();

  // How long `events` advertising events take: (events - 1) * interval + interval / 2.
  uint32_t burstMs(uint8_t events) const {
    if (events < 1) events = 1;
    return static_cast<uint32_t>(events - 1) * intervalMs_ + intervalMs_ / 2;
  }

 private:
  enum Mode : uint8_t { kOff, kBurst, kConsole };

  bool startManufacturerData(const uint8_t* data, uint8_t len);

  uint16_t intervalMs_ = 100;
  Mode mode_ = kOff;
};
