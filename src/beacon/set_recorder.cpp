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
  headMs_ = nowMs;
  wrapped_ = false;
  startMs_ = nowMs;
  lastAddMs_ = nowMs;
  countAbs_ = 0;
  countRel_ = 0;
}

void SetRecorder::dropOldest() {
  const size_t next = (head_ + 1) % kRecorderSamples;
  if (n_ > 1) headMs_ += static_cast<uint16_t>(s_t[next] - s_t[head_]);
  head_ = next;
  n_--;
  wrapped_ = true;
}

void SetRecorder::add(uint32_t nowMs, const int16_t accMg[3], const int16_t b[][3], uint8_t validBits) {
  if (n_ > 0 && nowMs - lastAddMs_ >= kRecorderMaxSpanMs) {
    // A gap longer than the ring may span (the loop was held up for a minute): everything before
    // it is too old to keep, and the 16-bit times could not tell how old.
    head_ = 0;
    n_ = 0;
    wrapped_ = true;
  }
  if (n_ == kRecorderSamples) dropOldest();  // full: the new sample replaces the oldest
  if (n_ == 0) headMs_ = nowMs;
  const size_t i = (head_ + n_) % kRecorderSamples;
  n_++;
  s_t[i] = static_cast<uint16_t>(nowMs - startMs_);
  for (uint8_t a = 0; a < 3; a++) s_acc[i][a] = accMg[a];
  for (uint8_t k = 0; k < kMaxMagnetometers; k++) {
    for (uint8_t a = 0; a < 3; a++) s_mag[k][i][a] = b[k][a];
  }
  s_flags[i] = validBits;
  lastAddMs_ = nowMs;
  while (n_ > 1 && nowMs - headMs_ > kRecorderMaxSpanMs) dropOldest();  // keep the span 16-bit
}

// Counted reps may arrive late and out of order (the rep check counts the first two together):
// keep them sorted by time.
void SetRecorder::addCount(uint32_t nowMs) {
  if (countAbs_ >= kRecorderMaxCounts) return;
  size_t i = countAbs_++;
  while (i > 0 && static_cast<int32_t>(s_countAbs[i - 1] - nowMs) > 0) {
    s_countAbs[i] = s_countAbs[i - 1];
    i--;
  }
  s_countAbs[i] = nowMs;
}

void SetRecorder::finish(uint32_t lastEventMs, gym::SetData* d) {
  if (head_ != 0) {  // the whole buffer, so that a ring that wrapped and then dropped by time works too
    rotate(s_t, kRecorderSamples, head_);
    rotateRows(s_acc, kRecorderSamples, head_);
    for (uint8_t k = 0; k < kMaxMagnetometers; k++) rotateRows(s_mag[k], kRecorderSamples, head_);
    rotate(s_flags, kRecorderSamples, head_);
    head_ = 0;
  }
  // Times relative to the first kept sample. The span is at most kRecorderMaxSpanMs (add()), under
  // 65.5 s, so 16-bit differences hold.
  const uint16_t t0 = n_ > 0 ? s_t[0] : 0;
  const uint16_t span = n_ > 0 ? static_cast<uint16_t>(s_t[n_ - 1] - t0) : 0;
  for (size_t i = 0; i < n_; i++) s_t[i] = static_cast<uint16_t>(s_t[i] - t0);
  const uint32_t firstMs = lastAddMs_ - span;
  firstMs_ = firstMs;

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
