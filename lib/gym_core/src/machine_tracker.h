// Gateway-side view of every configured machine, fed by decoded beacon packets (pure logic).
//
// This is the paper's "repetition vector": one slot per known beacon, updated from received
// packets. On top of the live count it decides where sets begin and end and whether a machine is
// in use, so the gateway only uploads when something changed.
//
// Set boundaries:
//   - a new set starts only when the beacon's set sequence changes or the rep count goes down;
//   - no packet for setGapMs finalizes the open set (its record is emitted);
//   - a later packet with the same set sequence reopens and amends that set (re-emitted with the
//     same setNumber, so an idempotent upload just overwrites it).
// In use: true once a set reaches minRepsForUse reps, false after idleMs without packets.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gym {

struct TrackerConfig {
  uint32_t setGapMs;       // no packet for this long finalizes the open set
  uint32_t idleMs;         // no packet for this long -> machine is no longer in use
  uint16_t minRepsForUse;  // sets below this are treated as bumps: no "in use", no record
};

struct MachineState {
  bool seen = false;          // any packet since boot
  bool hasSet = false;        // setSeq/setReps describe a real set
  bool setOpen = false;       // a set is in progress (not finalized)
  uint8_t setSeq = 0;         // beacon's sequence number of the current/last set
  uint16_t setReps = 0;       // highest rep count seen in the current/last set
  uint16_t lastSetReps = 0;   // reps of the last recorded (finalized) set
  uint32_t setNumber = 0;     // gateway-local set counter for this machine since boot
  bool inUse = false;
  int8_t rssi = 0;
  uint32_t lastPacketMs = 0;
  uint32_t setStartMs = 0;

  // Live rep count to display: 0 between sets (the paper's dashboard falls back to 0).
  uint16_t liveReps() const { return setOpen ? setReps : 0; }
};

struct SetRecord {
  uint8_t machine;
  uint32_t setNumber;
  uint8_t setSeq;
  uint16_t reps;
  uint32_t startMs;
  uint32_t endMs;
};

class MachineTracker {
 public:
  static const size_t kMaxMachines = 16;
  static const size_t kRingSize = 16;

  MachineTracker(size_t machineCount, const TrackerConfig& cfg);

  size_t machineCount() const { return count_; }
  const TrackerConfig& config() const { return cfg_; }

  void onPacket(size_t machine, uint8_t setSeq, uint16_t reps, int8_t rssi, uint32_t nowMs);
  void tick(uint32_t nowMs);

  const MachineState& state(size_t machine) const { return states_[machine]; }

  // Each returns whether the flag was set, and clears it.
  bool takeLiveDirty(size_t machine);   // live fields changed (reps/set/inUse/lastSetReps)
  bool takeUseChanged(size_t machine);  // inUse flipped

  // Oldest finalized set record first. Returns false when empty.
  bool popSet(SetRecord* out);
  size_t droppedSets() const { return dropped_; }

 private:
  void finalize(size_t machine);
  void pushSet(const SetRecord& rec);

  TrackerConfig cfg_;
  size_t count_;
  MachineState states_[kMaxMachines];
  bool liveDirty_[kMaxMachines];
  bool useChanged_[kMaxMachines];
  SetRecord ring_[kRingSize];
  size_t ringHead_ = 0;  // index of the oldest record
  size_t ringLen_ = 0;
  size_t dropped_ = 0;
};

}  // namespace gym
