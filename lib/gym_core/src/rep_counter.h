// Two-phase rep counter driven by accelerometer threshold interrupts (pure logic, no hardware).
//
// The LIS3DH is always armed for exactly one phase:
//
//   Count  --count interrupt-->  Rearm  --re-arm interrupt-->  Holdoff  --holdoffMs-->  Count
//
// A count-phase interrupt is one rep (paper: "the repetition number corresponds to the number of
// interrupts"). The re-arm phase is the opposite movement, so a pause, a slow phase that dips
// under the threshold twice, or a bounce after the re-arm event cannot be counted twice.
//
// Set handling follows the paper: reps accumulate until there has been no motion event for
// setTimeoutMs, then reset to zero. Every verified event (count or re-arm) restarts that timer,
// so slow tempos do not split a set.
#pragma once

#include <stdint.h>

namespace gym {

struct RepCounterConfig {
  uint32_t minRepIntervalMs;  // count events closer than this to the last counted rep are ignored
  uint32_t holdoffMs;         // quiet time after the re-arm event before counting is armed again
  uint32_t setTimeoutMs;      // no motion event for this long ends the set (paper: 5 s)
  uint16_t maxRepsPerSet;     // runaway guard
};

enum class RepPhase : uint8_t { Count, Rearm, Holdoff };

class RepCounter {
 public:
  explicit RepCounter(const RepCounterConfig& cfg);

  void setConfig(const RepCounterConfig& cfg) { cfg_ = cfg; }
  const RepCounterConfig& config() const { return cfg_; }

  // Sets the "last used" set sequence; the next set uses nextSetSeq(seq). Use a random seed at boot
  // so a rebooted beacon does not reuse the sequence number of the set the gateway last saw.
  void seedSetSeq(uint8_t seq) { setSeq_ = seq; }

  // Count-phase interrupt. Returns true if it counted a rep. Moves to Rearm either way.
  bool onCountEvent(uint32_t nowMs);

  // Re-arm-phase interrupt. Moves to Holdoff (or straight to Count when holdoffMs is 0).
  void onRearmEvent(uint32_t nowMs);

  // A lift started while no set is active (LIS3DH "wake" interrupt): the session around the
  // coming set starts now, so its samples begin at rest. Counts nothing and takes no set
  // sequence; the first count event turns it into the set. A wake with no count event within
  // setTimeoutMs expires quietly (poll() does not report it as a set end).
  void onWake(uint32_t nowMs);

  // Advances timers. Returns true if the set ended on this call (reps are then 0, phase Count).
  bool poll(uint32_t nowMs);

  // Earliest time poll() has work to do. Returns false if no timer is pending.
  bool nextDeadline(uint32_t* deadlineMs) const;

  // Ends the current set immediately (reps -> 0, phase -> Count). No-op when no set is active.
  void endSet();

  RepPhase phase() const { return phase_; }
  uint16_t reps() const { return reps_; }
  uint8_t setSeq() const { return setSeq_; }
  bool setActive() const { return setActive_; }
  bool woken() const { return woken_; }                      // a wake waits for its first count
  bool active() const { return setActive_ || woken_; }       // a session: woken, or a set
  uint32_t lastEventMs() const { return lastEventMs_; }

 private:
  RepCounterConfig cfg_;
  RepPhase phase_ = RepPhase::Count;
  bool setActive_ = false;
  bool woken_ = false;
  uint32_t wakeMs_ = 0;
  bool hasCounted_ = false;
  uint16_t reps_ = 0;
  uint8_t setSeq_ = 0;
  uint32_t lastCountMs_ = 0;
  uint32_t lastEventMs_ = 0;
  uint32_t holdoffStartMs_ = 0;
};

}  // namespace gym
