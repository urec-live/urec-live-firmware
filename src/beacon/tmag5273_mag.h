// Minimal TMAG5273 driver for weight sensing (TI 3D Hall-effect sensor; Adafruit #6489 carries
// the TMAG5273A1, +/-40/80 mT, at 0x35). Register values are from TI datasheet SLYS045C.
//
// The TMAG5273 is made for magnets (mT). The weight stack only bends the Earth's field by a few to
// tens of uT, which is close to its noise (32x averaging: 22 uT rms on X/Y, 9 uT on Z). So each
// 50 Hz sample averages several triggered 32x conversions, and the sensor sleeps (5 nA) between
// sets. We do not use the Adafruit library: it sets the wide range and a magnet temperature
// coefficient, and needs Adafruit_BusIO, which breaks `pio test -e beacon` (platformio.ini).
#pragma once

#include "magnetometer.h"

class Tmag5273Mag : public Magnetometer {
 public:
  Tmag5273Mag(uint8_t address, uint8_t oversample)
      : address_(address), oversample_(oversample < 1 ? 1 : oversample) {}

  bool begin(TwoWire& wire) override;
  bool wake() override;
  void sleep() override;
  bool read(int16_t b[3], int16_t* tempC10) override;
  const char* name() const override { return "TMAG5273"; }
  uint8_t address() const override { return address_; }
  uint8_t sensorId() const override { return 1; }
  void sampleNoise(int16_t out[3]) const override;

 private:
  bool configure();
  void wakeBus();
  bool convert(int32_t xyz[3], int32_t* tempCode);
  bool write(uint8_t reg, uint8_t value);
  bool readRegs(uint8_t reg, uint8_t* buf, uint8_t n);

  TwoWire* wire_ = nullptr;
  uint8_t address_;
  uint8_t oversample_;
  uint16_t rangeMt_ = 40;       // full scale of the low range: 40 mT (A1) or 133 mT (A2)
  uint8_t lastSetCount_ = 0xFF; // CONV_STATUS SET_COUNT of the last result we used
  bool needsInit_ = true;
};
