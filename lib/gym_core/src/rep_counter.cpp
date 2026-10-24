#include "rep_counter.h"

#include "gym_protocol.h"

namespace gym {

namespace {
// Wrap-safe "has `duration` passed since `since`" for millis()-style counters.
bool elapsed(uint32_t now, uint32_t since, uint32_t duration) {
  return static_cast<uint32_t>(now - since) >= duration;
}
}  // namespace

RepCounter::RepCounter(const RepCounterConfig& cfg) : cfg_(cfg) {}

bool RepCounter::onCountEvent(uint32_t nowMs) {
  if (phase_ != RepPhase::Count) return false;
  phase_ = RepPhase::Rearm;

  if (setActive_ && hasCounted_ && !elapsed(nowMs, lastCountMs_, cfg_.minRepIntervalMs)) {
    lastEventMs_ = nowMs;  // still motion: keeps the set alive
    return false;
  }
  if (!setActive_) {
    setActive_ = true;
    reps_ = 0;
    setSeq_ = nextSetSeq(setSeq_);
  }
  lastEventMs_ = nowMs;
  if (reps_ >= cfg_.maxRepsPerSet) return false;
  reps_++;
  hasCounted_ = true;
  lastCountMs_ = nowMs;
  return true;
}

void RepCounter::onRearmEvent(uint32_t nowMs) {
  if (phase_ != RepPhase::Rearm) return;
  if (setActive_) lastEventMs_ = nowMs;
  if (cfg_.holdoffMs == 0) {
    phase_ = RepPhase::Count;
  } else {
    phase_ = RepPhase::Holdoff;
    holdoffStartMs_ = nowMs;
  }
}

bool RepCounter::poll(uint32_t nowMs) {
  if (phase_ == RepPhase::Holdoff && elapsed(nowMs, holdoffStartMs_, cfg_.holdoffMs)) {
    phase_ = RepPhase::Count;
  }
  if (setActive_ && elapsed(nowMs, lastEventMs_, cfg_.setTimeoutMs)) {
    endSet();
    return true;
  }
  return false;
}

bool RepCounter::nextDeadline(uint32_t* deadlineMs) const {
  bool any = false;
  uint32_t best = 0;
  if (phase_ == RepPhase::Holdoff) {
    best = holdoffStartMs_ + cfg_.holdoffMs;
    any = true;
  }
  if (setActive_) {
    const uint32_t t = lastEventMs_ + cfg_.setTimeoutMs;
    // Compare relative to the last event so the choice survives millis() wrap-around.
    if (!any || static_cast<int32_t>(t - best) < 0) best = t;
    any = true;
  }
  if (any && deadlineMs != nullptr) *deadlineMs = best;
  return any;
}

void RepCounter::endSet() {
  if (!setActive_) return;
  setActive_ = false;
  hasCounted_ = false;
  reps_ = 0;
  phase_ = RepPhase::Count;
}

}  // namespace gym
