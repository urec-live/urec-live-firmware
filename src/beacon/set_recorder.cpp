#include "set_recorder.h"

namespace {
uint16_t s_t[kRecorderSamples];
int16_t s_acc[kRecorderSamples][3];
int16_t s_mag[kMaxMagnetometers][kRecorderSamples][3];
uint8_t s_flags[kRecorderSamples];
uint32_t s_countAbs[kRecorderMaxCounts];
uint16_t s_countRel[kRecorderMaxCounts];

template <typename T>
void reverse(T* a, size_t from, size_t to) {  // [from, to)
  while (from + 1 < to) {
    const T tmp = a[from];
    a[from++] = a[--to];
    a[to] = tmp;
  }
}

void reverseRows(int16_t (*a)[3], size_t from, size_t to) {
  while (from + 1 < to) {
    to--;
    for (uint8_t k = 0; k < 3; k++) {
      const int16_t tmp = a[from][k];
      a[from][k] = a[to][k];
      a[to][k] = tmp;
    }
    from++;
  }
}

// Rotates [0, n) left by `by` (three reversals, no extra memory).
template <typename T>
void rotate(T* a, size_t n, size_t by) {
  reverse(a, 0, by);
  reverse(a, by, n);
  reverse(a, 0, n);
}

void rotateRows(int16_t (*a)[3], size_t n, size_t by) {
  reverseRows(a, 0, by);
  reverseRows(a, by, n);
  reverseRows(a, 0, n);
}
}  // namespace

void SetRecorder::clear(uint32_t nowMs) {
  head_ = 0;
  n_ = 0;
  wrapped_ = false;
  startMs_ = nowMs;
  lastAddMs_ = nowMs;
  countAbs_ = 0;
  countRel_ = 0;
}

void SetRecorder::add(uint32_t nowMs, const int16_t accMg[3], const int16_t b[][3], uint8_t validBits) {
  size_t i;
  if (n_ < kRecorderSamples) {
    i = n_++;
  } else {  // full: overwrite the oldest sample
    i = head_;
    head_ = (head_ + 1) % kRecorderSamples;
    wrapped_ = true;
  }
  s_t[i] = static_cast<uint16_t>(nowMs - startMs_);
  for (uint8_t a = 0; a < 3; a++) s_acc[i][a] = accMg[a];
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
    for (uint8_t a = 0; a < 3; a++) s_mag[k][i][a] = b[k][a];
  }
  s_flags[i] = validBits;
  lastAddMs_ = nowMs;
}

void SetRecorder::addCount(uint32_t nowMs) {
  if (countAbs_ < kRecorderMaxCounts) s_countAbs[countAbs_++] = nowMs;
}

void SetRecorder::finish(uint32_t lastEventMs, gym::SetData* d) {
  if (head_ != 0) {
    rotate(s_t, n_, head_);
    rotateRows(s_acc, n_, head_);
    for (uint8_t k = 0; k < kMaxMagnetometers; k++) rotateRows(s_mag[k], n_, head_);
    rotate(s_flags, n_, head_);
    head_ = 0;
  }
  // Times relative to the first kept sample. The span is under 65.5 s, so 16-bit differences hold.
  const uint16_t t0 = n_ > 0 ? s_t[0] : 0;
  const uint16_t span = n_ > 0 ? static_cast<uint16_t>(s_t[n_ - 1] - t0) : 0;
  for (size_t i = 0; i < n_; i++) s_t[i] = static_cast<uint16_t>(s_t[i] - t0);
  const uint32_t firstMs = lastAddMs_ - span;

  countRel_ = 0;
  for (size_t k = 0; k < countAbs_; k++) {
    int32_t rel = static_cast<int32_t>(s_countAbs[k] - firstMs);
    // The count that started the recording can precede its first sample (the sampler was already
    // running for the stream and next took a sample a little later): it belongs at the start.
    if (rel < 0 && !wrapped_) rel = 0;
    if (rel >= 0 && rel <= span) s_countRel[countRel_++] = static_cast<uint16_t>(rel);
  }
  const int32_t lastEvent = static_cast<int32_t>(lastEventMs - firstMs);

  d->tMs = s_t;
  d->accMg = s_acc;
  d->n = n_;
  d->countMs = s_countRel;
  d->countN = countRel_;
  d->lastEventMs = lastEvent < 0 ? 0 : (lastEvent > 65535 ? 65535 : static_cast<uint16_t>(lastEvent));
}

gym::MagData SetRecorder::magData(uint8_t k, const int16_t noise[3]) const {
  gym::MagData m;
  m.b = s_mag[k];
  m.flags = s_flags;
  m.validBit = static_cast<uint8_t>(1u << k);
  for (uint8_t a = 0; a < 3; a++) m.noise[a] = noise[a];
  return m;
}

uint16_t SetRecorder::timeMs(size_t i) const { return s_t[i]; }
const int16_t* SetRecorder::acc(size_t i) const { return s_acc[i]; }
const int16_t* SetRecorder::mag(uint8_t k, size_t i) const { return s_mag[k][i]; }
uint8_t SetRecorder::flags(size_t i) const { return s_flags[i]; }
uint16_t SetRecorder::countMs(size_t i) const { return s_countRel[i]; }
