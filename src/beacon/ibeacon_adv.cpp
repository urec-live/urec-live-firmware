#include "ibeacon_adv.h"

#include <Arduino.h>
#include <bluefruit.h>

bool IBeaconAdvertiser::begin(int8_t txPowerDbm, uint16_t intervalMs) {
  intervalMs_ = intervalMs;
  if (!Bluefruit.begin()) return false;
  Bluefruit.autoConnLed(false);  // otherwise the blue LED blinks on every advertisement
  Bluefruit.setTxPower(txPowerDbm);
  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);
  Bluefruit.Advertising.restartOnDisconnect(false);
  Bluefruit.Advertising.setIntervalMS(intervalMs, intervalMs);
  // start(1) below: the SoftDevice stops by itself after 1 s even if we never call stop().
  Bluefruit.Advertising.setFastTimeout(1);
  return true;
}

bool IBeaconAdvertiser::burst(const gym::BeaconFrame& frame, uint8_t events) {
  uint8_t data[gym::kIBeaconDataLen];
  gym::encodeIBeacon(frame, data);

  // The SoftDevice must not be advertising from the buffer while we rewrite it.
  if (Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, data, sizeof(data));
  if (!Bluefruit.Advertising.start(1)) return false;

  if (events < 1) events = 1;
  delay(static_cast<uint32_t>(events - 1) * intervalMs_ + intervalMs_ / 2);
  if (Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.stop();
  return true;
}
