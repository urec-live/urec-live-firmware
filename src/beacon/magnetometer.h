// What the weight recorder needs from a magnetometer driver, so it does not care which breakout is
// on the STEMMA QT chain. Fields are in 0.1 uT (int16: +/-3276.7 uT, far above the Earth's field).
#pragma once

#include <Arduino.h>
#include <Wire.h>

class Magnetometer {
 public:
  // Checks the chip ID and writes the configuration, then puts the sensor to sleep.
  // Returns false if the chip does not answer.
  virtual bool begin(TwoWire& wire) = 0;

  // Gets ready to deliver samples (re-initialises after a fault). Returns false if the chip is gone.
  virtual bool wake() = 0;

  // Lowest-power state, between sets.
  virtual void sleep() = 0;

  // One 50 Hz sample: x, y, z (0.1 uT) and the die temperature (0.1 degC). false = no valid sample.
  virtual bool read(int16_t b[3], int16_t* tempC10) = 0;

  virtual const char* name() const = 0;
  virtual uint8_t address() const = 0;
  virtual uint8_t sensorId() const = 0;  // stored in the taught-weights file

  // RMS noise of one sample, per axis (0.1 uT): how still "still" is.
  virtual void sampleNoise(int16_t out[3]) const = 0;

 protected:
  ~Magnetometer() {}
};
