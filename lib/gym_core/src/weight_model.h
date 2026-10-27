// Weight classifier for the W8-Scope rep features (pure logic, no hardware).
//
// The paper trains a random forest offline; "the probability is computed first using each
// individual repetition, and then combined across the consecutive repetitions to determine the
// probability over an entire set". Here the weights are taught once on the beacon instead: every
// rep of a taught set becomes an example, each new rep votes with its 3 nearest examples, and the
// set's weight is the label most reps voted for (the paper's "dominant label").
//
// Distances are scaled per feature by the spread seen within one weight (never below the sensor's
// noise floor), so a noisy axis counts less than a clean one. A rep far from everything taught
// votes "unknown", and a set is only decided with >= 3 votes of which >= 60 % agree (the paper's
// confidence threshold t = 0.6). docs/weight-sensing.md §5 explains the choices.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "weight_pipeline.h"

namespace gym {

struct WeightEstimate {
  bool decided;     // a weight is reported
  uint16_t label;   // most voted label (tenths of the unit), meaningful when agree > 0
  uint8_t agree;    // reps that voted for `label`
  uint8_t votes;    // reps classified
  uint8_t unknown;  // reps unlike anything taught
};

class WeightModel {
 public:
  static const size_t kMaxExamples = 160;
  static const size_t kMaxLabels = 24;
  static const uint16_t kUnknown = 0xFFFF;
  static const size_t kHeaderBytes = 12;
  static const size_t kExampleBytes = 2 + 2 * kWeightFeatureCount;
  static const size_t kMaxSerializedBytes = kHeaderBytes + kMaxExamples * kExampleBytes;

  WeightModel();

  // Per-feature noise floor of one rep's features (0.1 uT for field features, mm for height).
  void setFloors(const int16_t floors[kWeightFeatureCount]);

  void clear();

  // Adds every rep as an example of `label`. When full, the oldest example of the most-taught label
  // makes room. Returns how many were added (0 if the label table is full).
  size_t add(uint16_t label, const RepFeatures* reps, size_t n);

  // Removes every example of `label`; returns how many.
  size_t forget(uint16_t label);

  size_t size() const { return n_; }

  // Distinct labels in ascending order with their example counts; returns how many (<= max).
  size_t labels(uint16_t* labels, uint16_t* counts, size_t max) const;

  // Label of the rep's nearest examples, or kUnknown when nothing taught is close.
  uint16_t vote(const RepFeatures& rep) const;

  WeightEstimate classify(const RepFeatures* reps, size_t n) const;

  // Little-endian image: header (magic "W8KM", format version, feature version, feature count,
  // sensor id, example count) then (label, features) per example. Returns bytes written, 0 if cap
  // is too small.
  size_t serialize(uint8_t sensorId, uint8_t* out, size_t cap) const;

  // Replaces the model with a serialized image. Rejects (and leaves the model empty) anything from
  // another sensor, feature layout or format version.
  bool deserialize(uint8_t sensorId, const uint8_t* in, size_t len);

 private:
  struct Example {
    uint16_t label;
    int16_t f[kWeightFeatureCount];
  };

  void refresh();
  void removeAt(size_t i);
  float dist2(const int16_t* a, const int16_t* b) const;

  Example ex_[kMaxExamples];
  size_t n_ = 0;
  int16_t floor_[kWeightFeatureCount];
  float invScale_[kWeightFeatureCount];
  float noveltyLimit2_ = 0.0f;  // squared scaled distance beyond which a rep is unknown
  float scratch_[kMaxExamples];
};

}  // namespace gym
