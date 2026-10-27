// Storage for one set's 50 Hz samples (accelerometer + magnetometers) and its counted reps, for the
// W8-Scope pipeline. The buffers are static (the loop task has a 4 KB stack).
//
// The buffer is a ring: a set longer than BEACON_W8_MAX_SET_S keeps its last part, because that
// part holds the stack at rest after the last rep, which the pipeline needs. finish() lays the ring
// out oldest-first and turns every time into "ms since the first kept sample".
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "beacon_config.h"
#include "weight_pipeline.h"

const uint8_t kMaxMagnetometers = 1;
const size_t kRecorderSamples = static_cast<size_t>(BEACON_W8_MAX_SET_S) * 50;
const size_t kRecorderMaxCounts = 100;

// Sample times are stored as 16-bit ms, so a recording must span well under 65.5 s.
static_assert(BEACON_W8_MAX_SET_S <= 60, "BEACON_W8_MAX_SET_S must be at most 60");

class SetRecorder {
 public:
  void clear(uint32_t nowMs);

  // One sample. b[k] is magnetometer k (0.1 uT); bit k of validBits says it is valid.
  void add(uint32_t nowMs, const int16_t accMg[3], const int16_t b[][3], uint8_t validBits);
  void addCount(uint32_t nowMs);

  // Call once after the last add(): fills d with the samples in time order, the counted reps
  // and the last motion event in the same time base.
  void finish(uint32_t lastEventMs, gym::SetData* d);

  gym::MagData magData(uint8_t k, const int16_t noise[3]) const;

  size_t size() const { return n_; }
  bool wrapped() const { return wrapped_; }

  // Rows for the CSV dump (valid after finish()).
  uint16_t timeMs(size_t i) const;
  const int16_t* acc(size_t i) const;
  const int16_t* mag(uint8_t k, size_t i) const;
  uint8_t flags(size_t i) const;
  size_t countN() const { return countRel_; }
  uint16_t countMs(size_t i) const;

 private:
  size_t head_ = 0;          // oldest sample
  size_t n_ = 0;
  bool wrapped_ = false;
  uint32_t startMs_ = 0;     // millis() at clear(); stored times are relative to it (mod 2^16)
  uint32_t lastAddMs_ = 0;
  size_t countAbs_ = 0;      // counted reps recorded (absolute millis())
  size_t countRel_ = 0;      // counted reps inside the kept samples, after finish()
};
