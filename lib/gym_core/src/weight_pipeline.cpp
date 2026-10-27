#include "weight_pipeline.h"

#include <math.h>

namespace gym {

namespace {
const float kMmPerS2PerMg = 9.80665f;
const float kPeakFraction = 0.6f;          // paper: delta = 60 % of the highest/lowest amplitude
const float kMinDipMg = 20.0f;             // shallower than this, the set has no usable reps
const float kIntegrateLowPassHz = 8.0f;    // light smoothing before integrating
const size_t kLowPassPad = 25;             // samples (0.5 s) of reflection to start each pass on
const uint16_t kRestDelayMs = 500;         // rest search starts this long after the last event
const uint16_t kRestWindowMs = 1500;
const uint16_t kSearchAfterCountMs = 1000; // valleys are searched up to the last count + this
const uint32_t kCountBeforeMs = 300;       // a counted rep's dip lies in [count - this,
const uint32_t kCountAfterMs = 600;        //                             count + this]
const uint16_t kMaxGapMs = 200;            // a longer gap between samples invalidates a segment
const float kLift5Mm = 50.0f;
const float kMinHeightMm = 50.0f;
const float kMinHeightFraction = 0.3f;     // of the median height: shorter segments are not reps
const int kZenithHalf = 5;                 // zenith field: mean of +/- 5 samples ...
const int kZenithMinValid = 7;             // ... of which at least 7 valid
const int kLift5Half = 2;
const int kLift5MinValid = 3;
const size_t kRestMaxSamples = 100;
const float kRestStableSigma = 2.0f;       // the two halves of the rest window must agree this well

uint16_t since(const uint16_t* t, size_t from, size_t to) { return static_cast<uint16_t>(t[to] - t[from]); }

float dtS(const uint16_t* t, size_t i) { return since(t, i - 1, i) * 0.001f; }

// Sorts v in place (insertion sort: n is small) and returns the median.
template <typename T>
T median(T* v, size_t n) {
  for (size_t i = 1; i < n; i++) {
    const T k = v[i];
    size_t j = i;
    while (j > 0 && v[j - 1] > k) {
      v[j] = v[j - 1];
      j--;
    }
    v[j] = k;
  }
  return v[n / 2];
}

int16_t saturate(float v) {
  if (v > 32767.0f) return 32767;
  if (v < -32767.0f) return -32767;
  return static_cast<int16_t>(lroundf(v));
}

// Zero-phase first-order low-pass (one pass forward, one backward). dt comes from the timestamps.
// Each pass first runs over an odd reflection of the samples at its starting end (2 y0 - y[j], as
// filtfilt pads), so the end is neither pulled toward its first value nor ramped in from zero. That
// matters at the start: the recording begins inside the first rep's dip.
void lowPass(float* y, const uint16_t* t, size_t n, float hz) {
  const float tau = 1.0f / (6.2831853f * hz);
  const size_t pad = n - 1 < kLowPassPad ? n - 1 : kLowPassPad;

  float s = 2.0f * y[0] - y[pad];
  for (size_t j = pad - 1; j >= 1; j--) {  // the mirror images of samples pad-1 .. 1
    const float dt = dtS(t, j + 1);
    s += dt / (tau + dt) * ((2.0f * y[0] - y[j]) - s);
  }
  float dt = dtS(t, 1);
  y[0] = s + dt / (tau + dt) * (y[0] - s);
  for (size_t i = 1; i < n; i++) {
    dt = dtS(t, i);
    y[i] = y[i - 1] + dt / (tau + dt) * (y[i] - y[i - 1]);
  }

  const size_t last = n - 1;
  s = 2.0f * y[last] - y[last - pad];
  for (size_t j = pad - 1; j >= 1; j--) {
    dt = dtS(t, last - j + 1);
    s += dt / (tau + dt) * ((2.0f * y[last] - y[last - j]) - s);
  }
  dt = dtS(t, last);
  y[last] = s + dt / (tau + dt) * (y[last] - s);
  for (size_t i = last; i-- > 0;) {
    dt = dtS(t, i + 1);
    y[i] = y[i + 1] + dt / (tau + dt) * (y[i] - y[i + 1]);
  }
}

// First window of kRestWindowMs, starting kRestDelayMs after the last motion event, in which every
// accelerometer axis stays within stillP2pMg peak-to-peak.
bool findRest(const SetData& d, uint16_t stillP2pMg, uint16_t* start, uint16_t* end) {
  const uint32_t from = static_cast<uint32_t>(d.lastEventMs) + kRestDelayMs;
  size_t i0 = 0;
  while (i0 < d.n && d.tMs[i0] < from) i0++;
  for (; i0 < d.n; i0++) {
    int16_t lo[3] = {32767, 32767, 32767};
    int16_t hi[3] = {-32768, -32768, -32768};
    bool still = true;
    size_t i = i0;
    for (; i < d.n && since(d.tMs, i0, i) < kRestWindowMs; i++) {
      for (uint8_t a = 0; a < 3; a++) {
        const int16_t v = d.accMg[i][a];
        if (v < lo[a]) lo[a] = v;
        if (v > hi[a]) hi[a] = v;
        if (hi[a] - lo[a] > stillP2pMg) still = false;
      }
      if (!still) break;
    }
    if (!still) continue;
    if (i >= d.n) return false;  // the buffer ends before the window is complete
    *start = static_cast<uint16_t>(i0);
    *end = static_cast<uint16_t>(i);
    return true;
  }
  return false;
}

// Rep tops: local minima of the filtered a_up below lowThr, at least minRepMs apart, with a peak
// above highThr since the previous valley (the paper's maxima; also the rep counter's re-arm rule).
// Without a peak in between, a second minimum belongs to the same top - a wobble in the dip, or the
// second dip of a pause at the top - and the first one stays: it is the end of the lift every time,
// so all valleys sit at the same phase. Two tops closer than minRepMs merge and the deeper valley
// wins (the paper's dT). Sample 0 counts when it is already inside a dip: the recording starts at a
// counted rep, so it often begins just past that top's lowest point. Never the last sample.
size_t findValleys(const float* y, const uint16_t* t, size_t searchEnd, float lowThr, float highThr,
                   uint16_t minRepMs, uint16_t* valleys, size_t cap) {
  size_t count = 0;
  bool peakSince = false;
  for (size_t i = 0; i <= searchEnd; i++) {
    if (count > 0 && y[i] > highThr) peakSince = true;
    const bool isMin = y[i] < lowThr && (i == 0 || y[i] <= y[i - 1]) && y[i] < y[i + 1];
    if (!isMin) continue;
    if (count == 0) {
      valleys[count++] = static_cast<uint16_t>(i);
      peakSince = false;
      continue;
    }
    if (!peakSince) continue;
    const uint16_t last = valleys[count - 1];
    if (since(t, last, i) >= minRepMs) {
      if (count == cap) break;
      valleys[count++] = static_cast<uint16_t>(i);
      peakSince = false;
    } else if (y[i] < y[last]) {
      valleys[count - 1] = static_cast<uint16_t>(i);
      peakSince = false;
    }
  }
  return count;
}

// Displacement over one valley-to-valley segment, written to x[s..e] (mm, 0 at both ends).
// Consecutive valleys are at the same phase of the motion, so velocity and position at both ends
// are equal: removing the mean acceleration enforces v(e) = v(s), removing the mean velocity
// enforces x(e) = x(s). That is exact whatever the velocity at the valley (the deepest dip is
// usually just before the true top) and cancels any constant accelerometer bias.
void integrateSegment(const float* a, const uint16_t* t, size_t s, size_t e, float* x) {
  const float total = since(t, s, e) * 0.001f;
  if (total <= 0.0f) return;
  float meanA = 0.0f;
  for (size_t i = s + 1; i <= e; i++) meanA += 0.5f * (a[i - 1] + a[i]) * dtS(t, i);
  meanA /= total;

  x[s] = 0.0f;  // velocity first (mm/s), in place
  for (size_t i = s + 1; i <= e; i++) {
    x[i] = x[i - 1] + (0.5f * (a[i - 1] + a[i]) - meanA) * kMmPerS2PerMg * dtS(t, i);
  }
  float meanV = 0.0f;
  for (size_t i = s + 1; i <= e; i++) meanV += 0.5f * (x[i - 1] + x[i]) * dtS(t, i);
  meanV /= total;

  float prevV = x[s];  // then displacement, replacing the velocity as it goes
  x[s] = 0.0f;
  for (size_t i = s + 1; i <= e; i++) {
    const float v = x[i];
    x[i] = x[i - 1] + (0.5f * (prevV + v) - meanV) * dtS(t, i);
    prevV = v;
  }
}

// The fastest moment of the lowering between the top at s and sample e: the minimum of the
// velocity over [s, e], integrated with the span's mean acceleration removed (both ends are at
// rest or at the same point of the motion). Returns 0 if the stack never moves down.
size_t fastestLowering(const float* a, const uint16_t* t, size_t s, size_t e) {
  const float total = since(t, s, e) * 0.001f;
  if (total <= 0.0f) return 0;
  float meanA = 0.0f;
  for (size_t i = s + 1; i <= e; i++) meanA += 0.5f * (a[i - 1] + a[i]) * dtS(t, i);
  meanA /= total;
  float v = 0.0f;
  float fastest = 0.0f;  // most negative velocity so far
  size_t at = 0;
  for (size_t i = s + 1; i <= e; i++) {
    v += (0.5f * (a[i - 1] + a[i]) - meanA) * dtS(t, i);
    if (v < fastest) {
      fastest = v;
      at = i;
    }
  }
  return at;
}

// Rises of at least 5 cm in x over [s, e], with 5 cm of hysteresis: one per lift.
size_t countLifts(const float* x, size_t s, size_t e) {
  size_t lifts = 0;
  bool rising = false;
  float lo = x[s];
  float hi = x[s];
  for (size_t i = s; i <= e; i++) {
    if (!rising) {
      if (x[i] < lo) lo = x[i];
      if (x[i] - lo >= kLift5Mm) {
        rising = true;
        lifts++;
        hi = x[i];
      }
    } else {
      if (x[i] > hi) hi = x[i];
      if (hi - x[i] >= kLift5Mm) {
        rising = false;
        lo = x[i];
      }
    }
  }
  return lifts;
}

bool hasGap(const uint16_t* t, size_t from, size_t to) {
  for (size_t i = from + 1; i <= to; i++) {
    if (since(t, i - 1, i) > kMaxGapMs) return true;
  }
  return false;
}

// True if y rises above highThr somewhere in [from, to]: the rise between two different tops.
bool peakBetween(const float* y, size_t from, size_t to, float highThr) {
  for (size_t i = from; i <= to; i++) {
    if (y[i] > highThr) return true;
  }
  return false;
}

// Tops the rep counter vouches for. A counted rep whose dip was shallower than 60 % of the deepest
// (often a slower first rep) has no valley; it gets the lowest point of its dip, if that is a real
// minimum inside [count - 300 ms, count + 600 ms] (the count fires early in the dip) and a peak
// separates it from the valleys on either side (findValleys' rule: otherwise it is a second dip of
// a top that already has its valley, e.g. a slow lift held at the top before a fast lowering).
// Returns the new valley count; the list stays sorted.
size_t addCountedTops(const float* y, const SetData& d, size_t searchEnd, float highThr, uint16_t* valleys,
                      size_t count, size_t cap) {
  for (size_t c = 0; c < d.countN && count < cap; c++) {
    const uint32_t ct = d.countMs[c];
    const uint32_t from = ct > kCountBeforeMs ? ct - kCountBeforeMs : 0;
    const uint32_t to = ct + kCountAfterMs;
    bool covered = false;
    for (size_t v = 0; v < count && !covered; v++) covered = d.tMs[valleys[v]] >= from && d.tMs[valleys[v]] <= to;
    if (covered) continue;
    size_t lo = 0;
    while (lo < searchEnd && d.tMs[lo] < from) lo++;
    size_t hi = lo;
    while (hi < searchEnd && d.tMs[hi + 1] <= to) hi++;
    size_t best = lo;
    for (size_t i = lo; i <= hi; i++) {
      if (y[i] < y[best]) best = i;
    }
    const bool interior = (best > lo || best == 0) && best < hi;  // not just the edge of a slope
    if (!interior || y[best] >= -kMinDipMg) continue;
    size_t p = count;  // insertion point
    while (p > 0 && valleys[p - 1] > best) p--;
    if (p > 0 && !peakBetween(y, valleys[p - 1], best, highThr)) continue;
    if (p < count && !peakBetween(y, best, valleys[p], highThr)) continue;
    for (size_t q = count; q > p; q--) valleys[q] = valleys[q - 1];
    valleys[p] = static_cast<uint16_t>(best);
    count++;
  }
  return count;
}

// dT matched to the counted tempo: 60 % of the median interval between counted reps, no shorter
// than the rep counter's own minimum interval.
uint16_t countedMinRep(const SetData& d, const WeightPipelineConfig& cfg) {
  uint16_t intervals[kMaxRepSegments];
  size_t m = 0;
  for (size_t k = 1; k < d.countN && m < kMaxRepSegments; k++) {
    intervals[m++] = static_cast<uint16_t>(d.countMs[k] - d.countMs[k - 1]);
  }
  uint32_t dt = static_cast<uint32_t>(median(intervals, m)) * 6 / 10;
  if (dt < cfg.minRepIntervalMs) dt = cfg.minRepIntervalMs;
  if (dt > cfg.minRepMs) dt = cfg.minRepMs;
  return static_cast<uint16_t>(dt);
}

bool fail(MotionAnalysis* out, const char* why) {
  out->error = why;
  return false;
}

// Collects the valid samples of one axis in [from, to) into buf; returns how many.
size_t collect(const MagData& m, uint8_t axis, size_t from, size_t to, int16_t* buf, size_t cap) {
  size_t k = 0;
  for (size_t i = from; i < to && k < cap; i++) {
    if (m.flags[i] & m.validBit) buf[k++] = m.b[i][axis];
  }
  return k;
}

// Mean field change over samples [c - half, c + half]; false if fewer than minValid are valid.
bool meanChange(const SetData& d, const MagData& m, size_t c, int half, int minValid,
                const int16_t rest[3], float out[3]) {
  float sum[3] = {0.0f, 0.0f, 0.0f};
  int valid = 0;
  for (int k = -half; k <= half; k++) {
    const long i = static_cast<long>(c) + k;
    if (i < 0 || i >= static_cast<long>(d.n) || (m.flags[i] & m.validBit) == 0) continue;
    for (uint8_t a = 0; a < 3; a++) sum[a] += m.b[i][a] - rest[a];
    valid++;
  }
  if (valid < minValid) return false;
  for (uint8_t a = 0; a < 3; a++) out[a] = sum[a] / valid;
  return true;
}
}  // namespace

bool analyzeMotion(const SetData& d, const WeightPipelineConfig& cfg, float* aUp, float* x,
                   MotionAnalysis* out) {
  out->error = nullptr;
  out->restStart = out->restEnd = 0;
  out->minRepMsUsed = cfg.minRepMs;
  out->adapted = false;
  out->valleyCount = out->segCount = out->validCount = 0;
  if (d.n < 50) return fail(out, "set too short");

  // 1. Rest window and gravity, from the set itself: same sensor mode, and no stale calibration.
  uint16_t rs = 0;
  uint16_t re = 0;
  if (!findRest(d, cfg.stillP2pMg, &rs, &re)) {
    return fail(out, "no still rest after the set (stack not down, or someone at the machine)");
  }
  out->restStart = rs;
  out->restEnd = re;
  float g[3] = {0.0f, 0.0f, 0.0f};
  for (size_t i = rs; i < re; i++) {
    for (uint8_t a = 0; a < 3; a++) g[a] += d.accMg[i][a];
  }
  for (uint8_t a = 0; a < 3; a++) {
    g[a] /= static_cast<float>(re - rs);
    out->gravityMg[a] = g[a];
  }
  const float gNorm = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
  if (gNorm < 500.0f) return fail(out, "accelerometer reads no gravity at rest");

  // 2. Vertical acceleration: the sign follows gravity, so an upside-down or tilted board works.
  for (size_t i = 0; i < d.n; i++) {
    aUp[i] = (d.accMg[i][0] * g[0] + d.accMg[i][1] * g[1] + d.accMg[i][2] * g[2]) / gNorm - gNorm;
  }

  // 3. Low-pass: a strong one for finding the reps, a light one for integrating.
  for (size_t i = 0; i < d.n; i++) x[i] = aUp[i];
  lowPass(x, d.tMs, d.n, cfg.lowPassHz);
  lowPass(aUp, d.tMs, d.n, kIntegrateLowPassHz);

  // 4. Valleys, searched up to just after the last counted rep: the landing after the last rep
  // and its ringing must not set the 60 % thresholds.
  size_t searchEnd = rs;
  if (d.countN > 0) {
    const uint32_t limit = static_cast<uint32_t>(d.countMs[d.countN - 1]) + kSearchAfterCountMs;
    size_t i = 0;
    while (i + 1 < d.n && d.tMs[i + 1] <= limit) i++;
    if (i < searchEnd) searchEnd = i;
  }
  if (searchEnd + 2 > d.n) searchEnd = d.n - 2;
  float lo = 0.0f;
  float hi = 0.0f;
  for (size_t i = 0; i <= searchEnd; i++) {
    if (x[i] < lo) lo = x[i];
    if (x[i] > hi) hi = x[i];
  }
  if (lo > -kMinDipMg || hi < kMinDipMg) return fail(out, "no reps in the accelerometer signal");
  const float lowThr = kPeakFraction * lo;
  const float highThr = kPeakFraction * hi;
  out->valleyCount =
      findValleys(x, d.tMs, searchEnd, lowThr, highThr, cfg.minRepMs, out->valleys, kMaxRepSegments + 1);

  // The paper's dT of 2 s merges faster reps. When the rep counter saw clearly more reps than
  // there are valleys, shorten dT to the counted tempo and search again.
  if (d.countN >= 3 && out->valleyCount + 1 < d.countN) {
    const uint16_t dt = countedMinRep(d, cfg);
    if (dt < cfg.minRepMs) {
      out->minRepMsUsed = dt;
      out->adapted = true;
      out->valleyCount = findValleys(x, d.tMs, searchEnd, lowThr, highThr, out->minRepMsUsed,
                                     out->valleys, kMaxRepSegments + 1);
    }
  }
  out->valleyCount =
      addCountedTops(x, d, searchEnd, highThr, out->valleys, out->valleyCount, kMaxRepSegments + 1);
  if (out->valleyCount < 2) return fail(out, "fewer than two rep tops found");

  // 5. Integration windows. A valley-to-valley segment is one rep (the paper's segmentation), but
  // the valley is a poor place to cut for integrating: the velocity changes fastest there, so a
  // valley found 20 ms early or late leaves the two ends at different speeds. The window runs
  // instead from the fastest moment of each lowering to the next one. There the velocity is at
  // its extreme, so it barely depends on where exactly the boundary falls, and it is the same
  // point of the motion whether or not the lifter holds at the top or the bottom. Each window
  // holds one bottom, one lift and its top. (The last lowering ends before the rest window.)
  uint16_t bound[kMaxRepSegments + 1];
  for (size_t k = 0; k < out->valleyCount; k++) {
    const size_t limit = k + 1 < out->valleyCount ? out->valleys[k + 1] : rs;
    bound[k] = static_cast<uint16_t>(fastestLowering(aUp, d.tMs, out->valleys[k], limit));  // 0: none
  }

  for (size_t i = 0; i < d.n; i++) x[i] = 0.0f;
  const size_t segs = out->valleyCount - 1;
  for (size_t k = 0; k < segs; k++) {
    if (bound[k] != 0 && bound[k + 1] != 0) integrateSegment(aUp, d.tMs, bound[k], bound[k + 1], x);
  }

  uint16_t heights[kMaxRepSegments];
  size_t heightCount = 0;
  for (size_t k = 0; k < segs; k++) {
    RepSegment& r = out->seg[k];
    const size_t s = bound[k];
    const size_t e = bound[k + 1];
    r.start = static_cast<uint16_t>(s);
    r.end = static_cast<uint16_t>(e);
    r.durationMs = since(d.tMs, out->valleys[k], out->valleys[k + 1]);
    r.bottom = r.zenith = r.lift5 = r.heightMm = 0;
    r.lift5Frac = 0;
    r.valid = false;
    if (s == 0 || e == 0) continue;

    size_t bottom = s;
    for (size_t i = s; i <= e; i++) {
      if (x[i] < x[bottom]) bottom = i;
    }
    size_t zenith = bottom;
    for (size_t i = bottom; i <= e; i++) {
      if (x[i] > x[zenith]) zenith = i;
    }
    const float height = x[zenith] - x[bottom];
    size_t lift5 = 0;
    float frac = 0.0f;  // where between samples lift5 - 1 and lift5 the 5 cm point lies
    for (size_t i = bottom + 1; i <= zenith; i++) {
      if (x[i] - x[bottom] >= kLift5Mm) {
        lift5 = i;
        const float step = x[i] - x[i - 1];
        frac = step > 0.0f ? (kLift5Mm - (x[i - 1] - x[bottom])) / step : 1.0f;
        break;
      }
    }
    r.bottom = static_cast<uint16_t>(bottom);
    r.zenith = static_cast<uint16_t>(zenith);
    r.lift5 = static_cast<uint16_t>(lift5);
    r.lift5Frac = static_cast<uint8_t>(lroundf(fminf(fmaxf(frac, 0.0f), 1.0f) * 255.0f));
    r.heightMm = height > 0.0f ? static_cast<uint16_t>(lroundf(height)) : 0;
    // A window that holds more than one lift spans reps whose tops were too shallow to find (e.g.
    // half reps the counter missed too); integrating across them collects too much drift. A rest
    // at the bottom inside the window is fine: it is still one lift.
    r.valid = lift5 != 0 && !hasGap(d.tMs, s, e) && countLifts(x, s, e) == 1;
    if (r.valid) heights[heightCount++] = r.heightMm;
  }
  out->segCount = segs;

  // Segments much lower than the typical lift are not reps (a pause, a bump, a partial top).
  if (heightCount > 0) {
    const float minHeight = fmaxf(kMinHeightMm, kMinHeightFraction * median(heights, heightCount));
    for (size_t k = 0; k < segs; k++) {
      RepSegment& r = out->seg[k];
      if (r.valid && r.heightMm < minHeight) r.valid = false;
      if (r.valid) out->validCount++;
    }
  }
  if (out->validCount == 0) return fail(out, "no rep lifted the stack by 5 cm");
  return true;
}

size_t magFeatures(const SetData& d, const MagData& m, const MotionAnalysis& motion, RepFeatures* out,
                   size_t maxOut, int16_t restOut[3], const char** error) {
  *error = nullptr;
  if (motion.error != nullptr) {
    *error = motion.error;
    return 0;
  }

  // The field at rest: median of the rest window, which must not drift between its halves.
  const size_t rs = motion.restStart;
  const size_t re = motion.restEnd;
  const size_t mid = rs + (re - rs) / 2;
  int16_t buf[kRestMaxSamples];
  for (uint8_t a = 0; a < 3; a++) {
    const size_t k = collect(m, a, rs, re, buf, kRestMaxSamples);
    if (k < 5 || 2 * k < re - rs) {
      *error = "magnetometer missed the rest window";
      return 0;
    }
    restOut[a] = median(buf, k);
    const size_t k1 = collect(m, a, rs, mid, buf, kRestMaxSamples);
    const int16_t first = k1 > 0 ? median(buf, k1) : restOut[a];
    const size_t k2 = collect(m, a, mid, re, buf, kRestMaxSamples);
    const int16_t second = k2 > 0 ? median(buf, k2) : restOut[a];
    const float noise = m.noise[a] > 0 ? m.noise[a] : 1;
    if (fabsf(static_cast<float>(first - second)) > kRestStableSigma * noise) {
      *error = "field not steady at rest (something moved near the sensor)";
      return 0;
    }
  }

  size_t w = 0;
  for (size_t k = 0; k < motion.segCount && w < maxOut; k++) {
    const RepSegment& r = motion.seg[k];
    if (!r.valid) continue;
    float zen[3];
    float lift5[3];
    if (!meanChange(d, m, r.zenith, kZenithHalf, kZenithMinValid, restOut, zen)) continue;
    // Exactly at 5 cm: the 5-sample means around the samples on either side, interpolated, so the
    // value does not depend on how fast the stack moved past it.
    float before[3];
    if (!meanChange(d, m, r.lift5 - 1, kLift5Half, kLift5MinValid, restOut, before)) continue;
    if (!meanChange(d, m, r.lift5, kLift5Half, kLift5MinValid, restOut, lift5)) continue;
    const float frac = r.lift5Frac / 255.0f;
    for (uint8_t a = 0; a < 3; a++) lift5[a] = before[a] + frac * (lift5[a] - before[a]);
    RepFeatures& f = out[w++];
    f.f[kFeatZenithX] = saturate(zen[0]);
    f.f[kFeatZenithY] = saturate(zen[1]);
    f.f[kFeatZenithZ] = saturate(zen[2]);
    f.f[kFeatLift5X] = saturate(lift5[0]);
    f.f[kFeatLift5Y] = saturate(lift5[1]);
    f.f[kFeatLift5Z] = saturate(lift5[2]);
    f.f[kFeatHeight] = static_cast<int16_t>(r.heightMm > 32767 ? 32767 : r.heightMm);
  }
  if (w == 0) *error = "no rep had enough magnetometer samples";
  return w;
}

}  // namespace gym
