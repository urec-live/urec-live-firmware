#include "machine_tracker.h"

namespace gym {

namespace {
bool elapsed(uint32_t now, uint32_t since, uint32_t duration) {
  return static_cast<uint32_t>(now - since) >= duration;
}
}  // namespace

MachineTracker::MachineTracker(size_t machineCount, const TrackerConfig& cfg)
    : cfg_(cfg), count_(machineCount) {
  if (count_ > kMaxMachines) count_ = kMaxMachines;
  for (size_t i = 0; i < kMaxMachines; i++) {
    liveDirty_[i] = false;
    useChanged_[i] = false;
  }
}

void MachineTracker::onPacket(size_t machine, uint8_t setSeq, uint16_t reps, int8_t rssi,
                              uint32_t nowMs) {
  if (machine >= count_) return;
  MachineState& st = states_[machine];
  st.seen = true;
  st.rssi = rssi;  // RSSI alone never marks the machine dirty
  if (reps == 0) {  // beacons only advertise once a rep is counted
    st.lastPacketMs = nowMs;
    return;
  }

  const bool newSet = !st.hasSet || setSeq != st.setSeq || reps < st.setReps;
  // Close the previous set before lastPacketMs moves on: its end is its own last packet.
  if (newSet && st.setOpen) finalize(machine);
  st.lastPacketMs = nowMs;
  if (newSet) {
    st.hasSet = true;
    st.setOpen = true;
    st.setSeq = setSeq;
    st.setReps = reps;
    st.setNumber++;
    st.setStartMs = nowMs;
    liveDirty_[machine] = true;
  } else {
    if (!st.setOpen) {  // packet for a set we already finalized: reopen and amend it
      st.setOpen = true;
      liveDirty_[machine] = true;
    }
    if (reps > st.setReps) {
      st.setReps = reps;
      liveDirty_[machine] = true;
    }
  }

  if (!st.inUse && st.setReps >= cfg_.minRepsForUse) {
    st.inUse = true;
    useChanged_[machine] = true;
    liveDirty_[machine] = true;
  }
}

void MachineTracker::tick(uint32_t nowMs) {
  for (size_t m = 0; m < count_; m++) {
    MachineState& st = states_[m];
    if (st.setOpen && elapsed(nowMs, st.lastPacketMs, cfg_.setGapMs)) finalize(m);
    if (st.inUse && elapsed(nowMs, st.lastPacketMs, cfg_.idleMs)) {
      st.inUse = false;
      useChanged_[m] = true;
      liveDirty_[m] = true;
    }
  }
}

void MachineTracker::finalize(size_t machine) {
  MachineState& st = states_[machine];
  st.setOpen = false;
  liveDirty_[machine] = true;
  if (st.setReps < cfg_.minRepsForUse) return;  // a bump of the stack, not a set
  st.lastSetReps = st.setReps;
  SetRecord rec;
  rec.machine = static_cast<uint8_t>(machine);
  rec.setNumber = st.setNumber;
  rec.setSeq = st.setSeq;
  rec.reps = st.setReps;
  rec.startMs = st.setStartMs;
  rec.endMs = st.lastPacketMs;
  pushSet(rec);
}

void MachineTracker::pushSet(const SetRecord& rec) {
  if (ringLen_ == kRingSize) {  // full: drop the oldest
    ringHead_ = (ringHead_ + 1) % kRingSize;
    ringLen_--;
    dropped_++;
  }
  ring_[(ringHead_ + ringLen_) % kRingSize] = rec;
  ringLen_++;
}

bool MachineTracker::popSet(SetRecord* out) {
  if (ringLen_ == 0) return false;
  if (out != nullptr) *out = ring_[ringHead_];
  ringHead_ = (ringHead_ + 1) % kRingSize;
  ringLen_--;
  return true;
}

bool MachineTracker::takeLiveDirty(size_t machine) {
  if (machine >= count_) return false;
  const bool d = liveDirty_[machine];
  liveDirty_[machine] = false;
  return d;
}

bool MachineTracker::takeUseChanged(size_t machine) {
  if (machine >= count_) return false;
  const bool d = useChanged_[machine];
  useChanged_[machine] = false;
  return d;
}

}  // namespace gym
