#include "ibeacon_adv.h"

#include <Arduino.h>
#include <bluefruit.h>

bool IBeaconAdvertiser::begin(int8_t txPowerDbm, uint16_t intervalMs) {
  intervalMs_ = intervalMs;
  if (!Bluefruit.begin()) return false;
  Bluefruit.autoConnLed(false);  // otherwise the blue LED blinks on every advertisement
  Bluefruit.setTxPower(txPowerDbm);
  Bluefruit.Advertising.restartOnDisconnect(false);  // loop() decides when to advertise again
  return true;
}

bool IBeaconAdvertiser::start(const gym::BeaconFrame& frame) {
  uint8_t data[gym::kIBeaconDataLen];
  gym::encodeIBeacon(frame, data);
  return startManufacturerData(data, sizeof(data));
}

bool IBeaconAdvertiser::start(const gym::StatusFrame& frame) {
  uint8_t data[gym::kStatusDataLen];
  gym::encodeStatus(frame, data);
  return startManufacturerData(data, sizeof(data));
}

bool IBeaconAdvertiser::startManufacturerData(const uint8_t* data, uint8_t len) {
  // The SoftDevice must not be advertising from the buffer while we rewrite it.
  stop();
  // Type, interval and timeout are set on every burst: the console advertisement changes them.
  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_NONCONNECTABLE_NONSCANNABLE_UNDIRECTED);
  Bluefruit.Advertising.setIntervalMS(intervalMs_, intervalMs_);
  // start(1) below: the SoftDevice stops by itself after 1 s even if we never call stop().
  Bluefruit.Advertising.setFastTimeout(1);
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();  // a non-scannable advertisement must not have one
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, data, len);
  if (!Bluefruit.Advertising.start(1)) return false;
  mode_ = kBurst;
  return true;
}

bool IBeaconAdvertiser::startConsole(BLEService& service, uint16_t intervalMs) {
  stop();
  Bluefruit.Advertising.setType(BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED);
  Bluefruit.Advertising.setIntervalMS(intervalMs, intervalMs);
  Bluefruit.Advertising.setFastTimeout(0);  // with start(0): no timeout
  Bluefruit.Advertising.clearData();
  Bluefruit.ScanResponse.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addService(service);  // 128-bit UUID: apps filter on it
  Bluefruit.ScanResponse.addName();           // no room left next to the UUID
  if (!Bluefruit.Advertising.start(0)) return false;
  mode_ = kConsole;
  return true;
}

void IBeaconAdvertiser::stop() {
  // After a phone connects the SoftDevice has already stopped the console advertisement.
  if (Bluefruit.Advertising.isRunning()) Bluefruit.Advertising.stop();
  mode_ = kOff;
}
