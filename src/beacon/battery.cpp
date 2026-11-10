#include "battery.h"

#include <Arduino.h>

#ifndef PIN_VBAT
#error "this board variant has no PIN_VBAT: the beacon expects a Feather nRF52840 Express"
#endif

namespace {
const uint32_t kSamples = 8;
const uint32_t kRangeMv = 3000;  // AR_INTERNAL_3_0: 0.6 V internal reference x 5
const uint32_t kFullScale = 4096;  // 12 bits
const uint32_t kDivider = 2;       // VBAT -> 1:2 divider -> A6
}  // namespace

uint16_t readBatteryMv() {
  // The internal reference does not depend on the 3.3 V rail, which sags as the battery runs down.
  analogReference(AR_INTERNAL_3_0);
  analogReadResolution(12);
  // The divider is a ~50 kOhm source: the default 3 us acquisition is too short to charge the
  // sampling capacitor through it and reads low (nRF52840 datasheet: 10 us for up to 100 kOhm).
  analogSampleTime(10);
  uint32_t sum = 0;
  for (uint32_t i = 0; i < kSamples; i++) sum += analogRead(PIN_VBAT);
  analogReference(AR_DEFAULT);
  analogReadResolution(10);
  analogSampleTime(3);
  return static_cast<uint16_t>((sum * kRangeMv * kDivider + kSamples * kFullScale / 2) / (kSamples * kFullScale));
}
