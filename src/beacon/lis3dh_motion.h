// Minimal LIS3DH driver for rep detection: inertial interrupt 1 on the INT1 pin plus raw reads.
//
// We do not use Adafruit_LIS3DH::begin(): it routes data-ready to INT1, enables high-resolution
// mode (not allowed together with low-power mode) and turns on the ADC. Register values are from
// the LIS3DH datasheet (DocID17530) and application note AN3308.
#pragma once

#include <Arduino.h>
#include <Wire.h>

class Lis3dhMotion {
 public:
  // INT1_SRC bits
  static const uint8_t kSrcIA = 0x40;

  struct Sample {
    int16_t mg[3];  // x, y, z in milli-g
  };

  // Checks WHO_AM_I and writes the base configuration. Returns false if no LIS3DH answers.
  bool begin(TwoWire& wire, uint8_t address);

  // 50 Hz, normal mode (10-bit, 4 mg), +/-2 g, BDU, HP filter off, IA1 -> INT1 latched, IA1 disabled.
  void configureBase();

  // Arms interrupt 1 for |a_axis| > thresholdMg (high) or < thresholdMg (!high), held for
  // `duration` samples. Order: CFG=0, THS, DUR, CFG. The caller then clears INT1_SRC.
  void arm(uint8_t axis, bool high, uint16_t thresholdMg, uint8_t duration);
  void disarm();

  // Reading INT1_SRC returns which condition fired (IA set) and clears the latched INT1 pin.
  uint8_t readInt1Source();

  // Burst read of STATUS_REG + OUT_X/Y/Z (L and H, as BDU requires). `fresh` = new XYZ data.
  bool readSample(Sample* s, bool* fresh);

  // Waits for the next fresh sample (one arrives every 20 ms).
  bool waitSample(Sample* s, uint32_t timeoutMs = 100);

  // Averages `n` fresh samples (after discarding one). p2pMg = max-min per axis.
  bool measure(uint8_t n, int16_t meanMg[3], int16_t p2pMg[3]);

  // INT1_SRC event bit for an axis/direction (also the INT1_CFG enable bit).
  static uint8_t eventBit(uint8_t axis, bool high) {
    return static_cast<uint8_t>(1u << (axis * 2 + (high ? 1 : 0)));
  }

 private:
  void write(uint8_t reg, uint8_t value);
  uint8_t read(uint8_t reg);

  TwoWire* wire_ = nullptr;
  uint8_t address_ = 0x18;
};
