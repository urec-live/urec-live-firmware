// W8-Scope per-set weight-sensing pipeline (pure logic, no hardware).
//
// Reproduces the signal processing of Radhakrishnan, Misra & Balan, "W8-Scope: Fine-grained,
// practical monitoring of weight stack-based exercises" (Pervasive and Mobile Computing 75, 2021):
// a magnetometer and an accelerometer on the top plate of the weight stack, where "the magnetic
// field strength is affected by this weight" and the accelerometer gives the lift height that
// disambiguates "(weight, height) combinations".
//
//   1. rest window after the last rep -> gravity vector and the field at rest
//   2. vertical acceleration a_up = a . g_hat - |g|
//   3. low-pass ("the raw accelerometer data is initially filtered")
//   4. valleys below 60 % of the lowest value, dT apart; "the segment between two consecutive
//      valleys is assumed to represent a repetition"
//   5. lift height by double integration of a_up, over one window per rep (mid-lowering to
//      mid-lowering, which holds that rep's lift and top)
//   6. per rep: field change at the top of the lift and 5 cm above its bottom
//
// docs/weight-sensing.md explains every step and where it deviates from the paper. All buffers are
// supplied by the caller; nothing here allocates or keeps state.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gym {

const uint8_t kWeightFeatureCount = 7;
const uint8_t kWeightFeatureVersion = 1;  // bump when the feature layout changes (model files)
const size_t kMaxRepSegments = 100;

// Feature order. Field values are changes from the field at rest, in 0.1 uT.
enum WeightFeatureIndex : uint8_t {
  kFeatZenithX,  // field change at the top of the lift
  kFeatZenithY,
  kFeatZenithZ,
  kFeatLift5X,   // field change 5 cm above the bottom of the lift, on the way up
  kFeatLift5Y,
  kFeatLift5Z,
  kFeatHeight,   // lift height, mm
};

struct RepFeatures {
  int16_t f[kWeightFeatureCount];
};

// One recorded set. Sample i was taken tMs[i] ms after sample 0 (tMs[0] == 0, increasing).
struct SetData {
  const uint16_t* tMs;
  const int16_t (*accMg)[3];  // accelerometer x, y, z (mg)
  size_t n;
  const uint16_t* countMs;    // counted reps (ms, same time base)
  size_t countN;
  uint16_t lastEventMs;       // last motion event of the rep counter (landing / last count)
};

// One magnetometer's samples for the same set.
struct MagData {
  const int16_t (*b)[3];  // x, y, z (0.1 uT)
  const uint8_t* flags;   // per sample
  uint8_t validBit;       // flags[i] & validBit: sample i is valid
  int16_t noise[3];       // rms noise of one sample per axis (0.1 uT)
};

struct WeightPipelineConfig {
  uint16_t minRepMs;          // paper: dT = 2 s between valleys
  uint16_t minRepIntervalMs;  // lower bound when dT is adapted to the counted tempo
  uint16_t stillP2pMg;        // rest window: max accelerometer peak-to-peak per axis
  float lowPassHz;            // segmentation low-pass cut-off
};

// One rep: the lift that ends at valley k + 1. Sample indices into the set.
struct RepSegment {
  uint16_t start;       // integration window: upward zero crossing of a_up after valley k ...
  uint16_t end;         // ... to the one after valley k + 1 (0 when there is none)
  uint16_t bottom;      // lowest point
  uint16_t zenith;      // highest point of the lift
  uint16_t lift5;       // first sample 5 cm above the bottom on the way up
  uint16_t heightMm;
  uint16_t durationMs;  // valley to valley (the rep's period)
  uint8_t lift5Frac;    // the 5 cm point lies this far (/255) from sample lift5 - 1 to lift5
  bool valid;
};

struct MotionAnalysis {
  const char* error;            // nullptr when the set is usable
  uint16_t restStart;           // still window after the last rep: [restStart, restEnd)
  uint16_t restEnd;
  float gravityMg[3];
  uint16_t minRepMsUsed;        // dT actually used
  bool adapted;                 // dT was shortened to the counted tempo
  uint16_t valleys[kMaxRepSegments + 1];
  size_t valleyCount;
  RepSegment seg[kMaxRepSegments];
  size_t segCount;
  size_t validCount;
};

// Steps 1-5. aUp and x are work arrays of d.n floats; on return x holds the displacement (mm,
// 0 at every window boundary) inside the integration windows. Returns false with out->error set
// when the set cannot be used.
bool analyzeMotion(const SetData& d, const WeightPipelineConfig& cfg, float* aUp, float* x,
                   MotionAnalysis* out);

// Step 6 for one magnetometer. Writes one feature vector per valid segment (at most maxOut) and
// returns how many; restOut is the field at rest (0.1 uT). Returns 0 with *error set when the
// magnetometer cannot be used for this set.
size_t magFeatures(const SetData& d, const MagData& m, const MotionAnalysis& motion, RepFeatures* out,
                   size_t maxOut, int16_t restOut[3], const char** error);

}  // namespace gym
