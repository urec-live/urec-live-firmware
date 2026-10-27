// Weight sensing on the beacon: W8-Scope (Radhakrishnan, Misra & Balan, Pervasive and Mobile
// Computing 2021) with a magnetometer on the LIS3DH's STEMMA QT chain. docs/weight-sensing.md
// explains it end to end.
//
// While a set is active the beacon samples the accelerometer and the magnetometer at 50 Hz. When
// the set ends (the rep counter's 5 s timeout) the recording runs through the paper's pipeline
// (lib/gym_core/src/weight_pipeline.h) and every rep votes for one of the weights taught with
// `teach` (weight_model.h). The estimate is printed on the serial console.
//
// The same 50 Hz sampler feeds the Teleplot stream. With no magnetometer on the bus and the stream
// off, none of this adds a timer, a wake-up or any I2C traffic to the rep counting.
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "lis3dh_motion.h"

class WeightSensing {
 public:
  struct Latest {
    int16_t accMg[3];
    int16_t b[3];  // 0.1 uT
    bool bValid;
  };

  // Probes the magnetometer and loads its taught weights. flashOk: the SoftDevice is running, so
  // flash writes complete (they would block forever otherwise).
  void begin(TwoWire& wire, Lis3dhMotion& imu, bool flashOk, uint32_t now);

  // While no set is active: looks again for a magnetometer that did not answer (every 30 s).
  void housekeeping(uint32_t now);

  // The 50 Hz sampler runs while a set is recorded or the Teleplot stream is on.
  bool sampling() const { return samplerOn_; }
  uint32_t nextSampleMs() const { return nextSampleMs_; }
  bool tick(uint32_t now);  // takes a sample when one is due; true if it did
  void setStreaming(bool on, uint32_t now);
  const Latest& latest() const { return latest_; }

  // Set lifecycle, driven by main.cpp from the rep counter. `blocked` is why this set must not be
  // weighed (lever mode), or nullptr.
  bool inSet() const { return inSet_; }
  void startSet(uint32_t now, const char* blocked);
  void onCount(uint32_t now);
  void finishSet(uint32_t now, uint32_t lastEventMs, uint8_t setSeq, uint16_t countedReps,
                 uint16_t minRepIntervalMs);
  void abortSet(uint32_t now);

  // Console.
  bool command(const char* cmd, const char* arg);
  void printHelp() const;
  void printSummary() const;  // one line for the `s` status
  bool dumping() const { return dumpSection_ != kDumpIdle; }
  void pumpDump();  // writes as much of a pending CSV dump as the USB buffer takes

 private:
  enum DumpSection : uint8_t { kDumpIdle, kDumpSummary, kDumpError, kDumpRest, kDumpCounts, kDumpValleys,
                               kDumpSegments, kDumpColumns, kDumpRows, kDumpEnd };

  void probe(uint32_t now);
  void updatePower(uint32_t now);
  void loadModel();
  bool saveModel();
  void printStatus() const;
  void printWeights() const;
  void startDump();
  bool nextDumpLine(char* line, size_t cap);

  TwoWire* wire_ = nullptr;
  Lis3dhMotion* imu_ = nullptr;
  bool flashOk_ = false;
  bool present_ = false;
  bool modelLoaded_ = false;
  bool magAwake_ = false;
  uint8_t magFails_ = 0;
  bool magLost_ = false;
  uint32_t lastProbeMs_ = 0;

  bool inSet_ = false;
  bool recording_ = false;
  bool streaming_ = false;
  bool samplerOn_ = false;
  const char* blocked_ = nullptr;
  uint32_t nextSampleMs_ = 0;
  uint32_t recordStartMs_ = 0;
  Latest latest_ = {};
  int16_t lastTempC10_ = 0;

  // The last processed set (for teach, dump and the status).
  bool haveSet_ = false;
  bool rawValid_ = false;     // the recorder still holds that set's samples (a later set reuses them)
  uint8_t lastEndedSeq_ = 0;  // the set that ended last, weighed or not
  uint8_t lastSetSeq_ = 0;
  uint16_t lastCounted_ = 0;
  bool haveRest_ = false;
  int16_t lastRest_[3] = {0, 0, 0};
  size_t lastFeatN_ = 0;
  bool lastTaught_ = false;
  const char* lastError_ = nullptr;
  bool logAll_ = false;

  DumpSection dumpSection_ = kDumpIdle;
  size_t dumpIndex_ = 0;
};
