// Weight sensing on the beacon: W8-Scope (Radhakrishnan, Misra & Balan, Pervasive and Mobile
// Computing 2021) with a magnetometer on the LIS3DH's STEMMA QT chain. docs/weight-sensing.md
// explains it end to end.
//
// While a set is active the beacon samples the accelerometer and the magnetometer(s) at 50 Hz. When
// the set ends (the rep counter's 5 s timeout) the recording runs through the paper's pipeline
// (lib/gym_core/src/weight_pipeline.h) and every rep votes for one of the weights taught with
// `teach` (weight_model.h). With both a TMAG5273 and an LIS2MDL fitted, each one is classified
// separately and the results are printed side by side. Every recorded set is then also kept in
// the QSPI flash (set_store.h), for a download of the whole session later.
//
// The same 50 Hz sampler feeds the Teleplot stream. With no magnetometer on the bus and the stream
// off, none of this adds a timer, a wake-up or any I2C traffic to the rep counting.
#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "lis3dh_motion.h"
#include "rep_validator.h"
#include "weight_settings.h"

class WeightSensing {
 public:
  // Probes the magnetometers, loads their taught weights and finds the set store (set_store.h).
  // flashOk: the SoftDevice is running, so writes to the internal flash complete (they would block
  // forever otherwise). bootId: random, stored with each set to tell power-ups apart.
  void begin(TwoWire& wire, Lis3dhMotion& imu, bool flashOk, uint32_t now, uint16_t bootId);

  // While no set is active: looks again for a magnetometer that did not answer (every 30 s).
  void housekeeping(uint32_t now);

  // The 50 Hz sampler runs while a set is recorded, the Teleplot stream is on, or the rep check
  // follows the stack (motion sampling: the accelerometer only, magnetometer or not).
  bool sampling() const { return samplerOn_; }
  uint32_t nextSampleMs() const { return nextSampleMs_; }
  bool tick(uint32_t now);  // takes a sample when one is due; true if it did
  void setStreaming(bool on, uint32_t now);
  void setMotionSampling(bool on, uint32_t now);
  const int16_t* latestAccMg() const { return latestAcc_; }
  void printStreamFields() const;  // Teleplot lines for the latest field sample(s)

  // Set lifecycle, driven by main.cpp from the rep counter: the recording runs from the lift-start
  // wake (or the first dip) to the set's end. `blocked` is why this set must not be weighed (lever
  // mode), or nullptr. onCount: a counted rep, at its dip's time (it may be counted later).
  bool inSet() const { return inSet_; }
  void startSet(uint32_t now, const char* blocked);
  void onCount(uint32_t dipMs);
  // The rep check's view of the set, for its CSV: the resting reading it started from, whether
  // the wake started it, and every dip with its verdict. Call before finishSet().
  void setMoves(const int16_t restMg[3], bool wake, const gym::RepValidator& v);
  void finishSet(uint32_t now, uint32_t lastEventMs, uint8_t setSeq, uint16_t countedReps,
                 uint16_t minRepIntervalMs);
  void abortSet(uint32_t now, bool quiet = false);  // quiet: a wake with no rep, nothing to say

  // Console. fromPhone: the command came from the Bluetooth console, which may not erase the
  // stored sets or start a download (both are USB-only).
  bool command(const char* cmd, const char* arg, bool fromPhone);
  void printHelp() const;
  void printSummary() const;  // the weight and store lines of the `s` status
  bool dumping() const;       // a CSV dump (`dump`) or a download (`dumpall`) is going to USB
  void pumpDump();  // writes as much of a pending CSV dump as the USB buffer takes

 private:
  enum DumpSection : uint8_t { kDumpIdle, kDumpSummary, kDumpError, kDumpRest, kDumpMagRest, kDumpCounts,
                               kDumpCal, kDumpMoves, kDumpValleys, kDumpSegments, kDumpColumns, kDumpRows,
                               kDumpEnd };
  struct DumpCursor {
    DumpSection section = kDumpIdle;
    size_t index = 0;
  };

  void probe(uint32_t now);
  void updatePower(uint32_t now);
  bool anyPresent() const;
  size_t featTotal() const;
  void report(uint8_t setSeq) const;
  void printStatus() const;
  void printSummaryWeight() const;
  void printWeights() const;
  void startDump();
  // The next line of the last set's CSV dump at cursor `c` (with its newline); false when a
  // section ends without a line. The live dump and the set store each walk their own cursor.
  bool nextDumpLine(DumpCursor& c, char* line, size_t cap) const;
  void storeSet(uint8_t setSeq, uint32_t now);  // into the set store, right after the report

  TwoWire* wire_ = nullptr;
  Lis3dhMotion* imu_ = nullptr;
  bool flashOk_ = false;
  uint32_t lastProbeMs_ = 0;

  bool inSet_ = false;
  bool recording_ = false;
  bool streaming_ = false;
  bool motionSampling_ = false;
  bool samplerOn_ = false;
  bool magInSet_ = false;  // a magnetometer was there when the set started
  const char* blocked_ = nullptr;
  uint32_t nextSampleMs_ = 0;
  uint32_t recordStartMs_ = 0;
  int16_t latestAcc_[3] = {0, 0, 0};

  // The last processed set (for teach, dump and the status). Per-sensor results live in the slots.
  bool haveSet_ = false;
  bool rawValid_ = false;     // the recorder still holds that set's samples (a later set reuses them)
  uint8_t lastEndedSeq_ = 0;  // the set that ended last, weighed or not
  uint8_t lastSetSeq_ = 0;
  uint16_t lastCounted_ = 0;
  bool lastTaught_ = false;
  const char* motionError_ = nullptr;
  bool logAll_ = false;

  DumpCursor dump_;  // the live `dump`
};
