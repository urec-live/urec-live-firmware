#include "weight_model.h"

#include <math.h>
#include <string.h>

namespace gym {

namespace {
const uint8_t kMagic[4] = {'W', '8', 'K', 'M'};
const uint8_t kFormatVersion = 1;
const size_t kNeighbours = 3;
const float kNoveltyFactor = 3.0f;    // unknown beyond 3x the typical nearest-neighbour distance,
const float kMinNoveltyLimit = 3.0f;  // and never closer than 3 noise floors
const float kFar = 3.0e38f;

void put16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v & 0xFF);
  p[1] = static_cast<uint8_t>(v >> 8);
}

uint16_t get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
}  // namespace

WeightModel::WeightModel() {
  for (uint8_t f = 0; f < kWeightFeatureCount; f++) {
    floor_[f] = 1;
    invScale_[f] = 1.0f;
  }
}

void WeightModel::setFloors(const int16_t floors[kWeightFeatureCount]) {
  for (uint8_t f = 0; f < kWeightFeatureCount; f++) floor_[f] = floors[f] > 0 ? floors[f] : 1;
  refresh();
}

void WeightModel::clear() {
  n_ = 0;
  refresh();
}

void WeightModel::removeAt(size_t i) {
  for (size_t j = i + 1; j < n_; j++) ex_[j - 1] = ex_[j];
  n_--;
}

size_t WeightModel::add(uint16_t label, const RepFeatures* reps, size_t n) {
  if (label == kUnknown) return 0;
  uint16_t ls[kMaxLabels];
  uint16_t cs[kMaxLabels];
  const size_t distinct = labels(ls, cs, kMaxLabels);
  bool known = false;
  for (size_t i = 0; i < distinct; i++) known = known || ls[i] == label;
  if (!known && distinct >= kMaxLabels) return 0;

  size_t added = 0;
  for (size_t r = 0; r < n; r++) {
    if (n_ == kMaxExamples) {
      // Make room: the oldest example of the most-taught label keeps the labels balanced.
      const size_t k = labels(ls, cs, kMaxLabels);
      size_t most = 0;
      for (size_t i = 1; i < k; i++) {
        if (cs[i] > cs[most]) most = i;
      }
      size_t victim = 0;
      while (victim < n_ && ex_[victim].label != ls[most]) victim++;
      removeAt(victim);
    }
    ex_[n_].label = label;
    memcpy(ex_[n_].f, reps[r].f, sizeof(ex_[n_].f));
    n_++;
    added++;
  }
  refresh();
  return added;
}

size_t WeightModel::forget(uint16_t label) {
  size_t removed = 0;
  size_t w = 0;
  for (size_t i = 0; i < n_; i++) {
    if (ex_[i].label == label) {
      removed++;
    } else {
      ex_[w++] = ex_[i];
    }
  }
  n_ = w;
  refresh();
  return removed;
}

size_t WeightModel::labels(uint16_t* out, uint16_t* counts, size_t max) const {
  size_t k = 0;
  for (size_t i = 0; i < n_; i++) {
    const uint16_t l = ex_[i].label;
    size_t j = 0;
    while (j < k && out[j] < l) j++;
    if (j < k && out[j] == l) {
      counts[j]++;
      continue;
    }
    if (k == max) continue;
    for (size_t m = k; m > j; m--) {
      out[m] = out[m - 1];
      counts[m] = counts[m - 1];
    }
    out[j] = l;
    counts[j] = 1;
    k++;
  }
  return k;
}

float WeightModel::dist2(const int16_t* a, const int16_t* b) const {
  float sum = 0.0f;
  for (uint8_t f = 0; f < kWeightFeatureCount; f++) {
    const float d = (a[f] - b[f]) * invScale_[f];
    sum += d * d;
  }
  return sum;
}

// Feature scales (pooled spread within each label, never below the floor) and the novelty limit
// (3x the median distance from each example to its nearest other example).
void WeightModel::refresh() {
  uint16_t ls[kMaxLabels];
  uint16_t cs[kMaxLabels];
  const size_t k = labels(ls, cs, kMaxLabels);
  for (uint8_t f = 0; f < kWeightFeatureCount; f++) {
    float ss = 0.0f;
    size_t dof = 0;
    for (size_t l = 0; l < k; l++) {
      if (cs[l] < 2) continue;
      float mean = 0.0f;
      for (size_t i = 0; i < n_; i++) {
        if (ex_[i].label == ls[l]) mean += ex_[i].f[f];
      }
      mean /= cs[l];
      for (size_t i = 0; i < n_; i++) {
        if (ex_[i].label != ls[l]) continue;
        const float d = ex_[i].f[f] - mean;
        ss += d * d;
      }
      dof += cs[l] - 1;
    }
    float sigma = 0.0f;
    if (dof > 0) {
      sigma = sqrtf(ss / dof);
    } else if (n_ >= 2) {  // no label taught twice yet: overall spread
      float mean = 0.0f;
      for (size_t i = 0; i < n_; i++) mean += ex_[i].f[f];
      mean /= n_;
      for (size_t i = 0; i < n_; i++) {
        const float d = ex_[i].f[f] - mean;
        sigma += d * d;
      }
      sigma = sqrtf(sigma / (n_ - 1));
    }
    invScale_[f] = 1.0f / fmaxf(sigma, static_cast<float>(floor_[f]));
  }

  float limit = kMinNoveltyLimit;
  if (n_ >= 2) {
    for (size_t i = 0; i < n_; i++) {
      float nearest = kFar;
      for (size_t j = 0; j < n_; j++) {
        if (j == i) continue;
        const float d = dist2(ex_[i].f, ex_[j].f);
        if (d < nearest) nearest = d;
      }
      // insertion into the sorted scratch list
      size_t p = i;
      while (p > 0 && scratch_[p - 1] > nearest) {
        scratch_[p] = scratch_[p - 1];
        p--;
      }
      scratch_[p] = nearest;
    }
    limit = fmaxf(kNoveltyFactor * sqrtf(scratch_[n_ / 2]), kMinNoveltyLimit);
  }
  noveltyLimit2_ = limit * limit;
}

uint16_t WeightModel::vote(const RepFeatures& rep) const {
  if (n_ == 0) return kUnknown;
  float best[kNeighbours] = {kFar, kFar, kFar};
  size_t idx[kNeighbours] = {0, 0, 0};
  for (size_t i = 0; i < n_; i++) {
    const float d = dist2(rep.f, ex_[i].f);
    if (d >= best[kNeighbours - 1]) continue;
    size_t p = kNeighbours - 1;
    while (p > 0 && best[p - 1] > d) {
      best[p] = best[p - 1];
      idx[p] = idx[p - 1];
      p--;
    }
    best[p] = d;
    idx[p] = i;
  }
  if (best[0] > noveltyLimit2_) return kUnknown;

  // Majority of the neighbours within the novelty limit; a tie goes to the closer label.
  uint16_t lab[kNeighbours];
  uint8_t cnt[kNeighbours];
  float weight[kNeighbours];
  size_t m = 0;
  for (size_t j = 0; j < kNeighbours && j < n_; j++) {
    if (best[j] > noveltyLimit2_) break;
    const uint16_t l = ex_[idx[j]].label;
    size_t q = 0;
    while (q < m && lab[q] != l) q++;
    if (q == m) {
      lab[m] = l;
      cnt[m] = 0;
      weight[m] = 0.0f;
      m++;
    }
    cnt[q]++;
    weight[q] += 1.0f / (sqrtf(best[j]) + 1e-3f);
  }
  size_t win = 0;
  for (size_t q = 1; q < m; q++) {
    if (cnt[q] > cnt[win] || (cnt[q] == cnt[win] && weight[q] > weight[win])) win = q;
  }
  return lab[win];
}

WeightEstimate WeightModel::classify(const RepFeatures* reps, size_t n) const {
  WeightEstimate e = {false, 0, 0, 0, 0};
  uint16_t ls[kMaxLabels];
  uint8_t cs[kMaxLabels];
  size_t k = 0;
  if (n > 255) n = 255;
  for (size_t r = 0; r < n; r++) {
    const uint16_t v = vote(reps[r]);
    e.votes++;
    if (v == kUnknown) {
      e.unknown++;
      continue;
    }
    size_t q = 0;
    while (q < k && ls[q] != v) q++;
    if (q == k) {
      if (k == kMaxLabels) continue;
      ls[k] = v;
      cs[k] = 0;
      k++;
    }
    cs[q]++;
  }
  if (k == 0) return e;
  size_t win = 0;
  bool tie = false;
  for (size_t q = 1; q < k; q++) {
    if (cs[q] > cs[win]) {
      win = q;
      tie = false;
    } else if (cs[q] == cs[win]) {
      tie = true;
    }
  }
  e.label = ls[win];
  e.agree = cs[win];
  e.decided = !tie && e.votes >= 3 && e.agree * 10 >= 6 * e.votes;
  return e;
}

size_t WeightModel::serialize(uint8_t sensorId, uint8_t* out, size_t cap) const {
  const size_t need = kHeaderBytes + n_ * kExampleBytes;
  if (cap < need) return 0;
  memcpy(out, kMagic, sizeof(kMagic));
  out[4] = kFormatVersion;
  out[5] = kWeightFeatureVersion;
  out[6] = kWeightFeatureCount;
  out[7] = sensorId;
  put16(out + 8, static_cast<uint16_t>(n_));
  put16(out + 10, 0);
  uint8_t* p = out + kHeaderBytes;
  for (size_t i = 0; i < n_; i++) {
    put16(p, ex_[i].label);
    p += 2;
    for (uint8_t f = 0; f < kWeightFeatureCount; f++) {
      put16(p, static_cast<uint16_t>(ex_[i].f[f]));
      p += 2;
    }
  }
  return need;
}

bool WeightModel::deserialize(uint8_t sensorId, const uint8_t* in, size_t len) {
  n_ = 0;
  bool ok = len >= kHeaderBytes && memcmp(in, kMagic, sizeof(kMagic)) == 0 && in[4] == kFormatVersion &&
            in[5] == kWeightFeatureVersion && in[6] == kWeightFeatureCount && in[7] == sensorId;
  const size_t count = ok ? get16(in + 8) : 0;
  ok = ok && count <= kMaxExamples && len >= kHeaderBytes + count * kExampleBytes;
  if (ok) {
    const uint8_t* p = in + kHeaderBytes;
    for (size_t i = 0; i < count; i++) {
      ex_[i].label = get16(p);
      p += 2;
      for (uint8_t f = 0; f < kWeightFeatureCount; f++) {
        ex_[i].f[f] = static_cast<int16_t>(get16(p));
        p += 2;
      }
    }
    n_ = count;
  }
  refresh();
  return ok;
}

}  // namespace gym
