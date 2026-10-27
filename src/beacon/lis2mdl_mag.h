// Minimal LIS2MDL driver for weight sensing (ST 3-axis magnetometer, Adafruit #4488 at 0x1E).
// Register values are from the ST datasheet (DocID030621).
//
// The LIS2MDL is an Earth-field magnetometer like the paper's BMM150: 0.15 uT per step and 0.3 uT
// rms noise with offset cancellation, against the TMAG5273's 9-22 uT. It samples continuously at
// 50 Hz (575 uA) while a set is recorded and idles (1.5 uA) between sets. We do not use the
// Adafruit library: it needs Adafruit_BusIO, which breaks `pio test -e beacon` (platformio.ini).
#pragma once

#include "magnetometer.h"

class Lis2mdlMag : public Magnetometer {
 public:
  explicit Lis2mdlMag(uint8_t address) : address_(address) {}

  bool begin(TwoWire& wire) override;
  bool wake() override;
  void sleep() override;
  bool read(int16_t b[3], int16_t* tempC10) override;
  const char* name() const override { return "LIS2MDL"; }
  uint8_t address() const override { return address_; }
  uint8_t sensorId() const override { return 2; }
  void sampleNoise(int16_t out[3]) const override;

 private:
  bool configure();
  bool write(uint8_t reg, uint8_t value);
  bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n);

  TwoWire* wire_ = nullptr;
  uint8_t address_;
  uint32_t readyAtMs_ = 0;  // samples before this are still settling after wake()
};
