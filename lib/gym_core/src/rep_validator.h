// Checks that each counted dip belongs to a real rep cycle (pure logic, no hardware).
//
// The rep counter (rep_counter.h) counts one LIS3DH "dip" per rep: the deceleration at the top of
// the lift. Any downward acceleration is a dip, so setting up (lifting the stack and holding it) and
// letting go after the last rep (the stack falls) are dips too. This class follows the stack's
// position from the 50 Hz samples and keeps a dip only if the stack went up and came back down by
// an amount like the set's other reps:
//
//   setup    a rise with no return                 -> "no return" (or merged into rep 1's rise)
//   release  a dip while the stack is going down   -> "no rise"
//   fidget   a small up-and-down                   -> "small"
//   handling the beacon tilted far from its rest   -> "tilted"
//
// Signal: a = acc . u - g0, the acceleration along gravity (u = unit low-passed acc vector), so a
// tilted beacon works and side-to-side rattle does not bias it. The stack's motion is split into
// strokes where a leaky velocity changes sign; each stroke is integrated from v = 0 and de-drifted so
// v = 0 at its end too, which cancels any constant accelerometer bias. The leaky velocity turns a
// little before the stack does; if the signal goes quiet soon after a stroke boundary, the stack
// has stopped (a hold) and its velocity is 0 from there (checkStop). Turning points (peaks and
// valleys, with hysteresis) of the summed strokes give each dip its rise and its return.
//
// Counting: the first two cycles are held until both have come back down and agree; then they
// count together (the count jumps 0 -> 2). From then on a dip counts at once if the rise so far
// reaches the set's template (matchPct of the median travel of the counted reps, at least
// minTravelMm). docs/rep-counting-walkthrough.md §3.7 explains it; tools/replay_reps/ replays
// downloaded sets through it.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace gym {

struct RepValidatorConfig {
  uint16_t minTravelMm = 80;   // a rep moves the stack at least this far up and back down
  uint8_t matchPct = 50;       // ... and at least this % of the median travel of the counted reps
  uint8_t startMatchPct = 60;  // the first two cycles' returns must agree this well
  uint16_t turnMm = 40;        // turning-point hysteresis: smaller wiggles are not strokes
  uint8_t tiltDeg = 35;        // tilted further than this from the rest direction: handling
  uint16_t lateMs = 500;       // a dip this soon after its lift ended still belongs to that lift
  // Signal processing (tuned on the gym recordings; not on the console)
  float dirHz = 2.0f;          // low-pass of the gravity direction used for the projection
  float lowPassHz = 4.0f;      // low-pass on a
  float leakS = 1.0f;          // leaky velocity (direction only)
  float biasS = 6.0f;          // slow bias tracker on a
  float switchMps = 0.06f;     // leaky velocity needed to switch direction
  float tiltRefS = 4.0f;       // the tilt reference follows the gravity direction this slowly
  // A stroke boundary falls a little before the stack really turns (the leaky velocity leads). If
  // the signal then goes quiet soon after the boundary, the stack has stopped (a hold): its
  // velocity is reset to 0 there, and what was left over corrects both strokes.
  float quietMps2 = 0.3f;      // |a| below this is quiet
  uint16_t quietMs = 300;      // ... for this long
  uint16_t stopWithinMs = 500; // ... starting this soon after the stroke boundary
  float stopMaxMps = 0.4f;     // a leftover velocity larger than this is not a stop
};

enum class MoveVerdict : uint8_t {
  Pending,     // waiting for its peak or return
  Held,        // first cycle: waiting for a second one to agree with
  Counted,     // checked and counted
  Fast,        // counted at the dip: its rise already matched the set's reps
  Unchecked,   // counted without a check (the session started at this dip, mid-motion)
  NoRise,      // the stack was going down (release, stand-up) or did not rise
  NoReturn,    // rose but did not come back down (setup)
  Small,       // rose and came back, but less than the set's reps
  Superseded,  // a later dip in the same lift took its place (setup merging into rep 1)
  Dropped,     // start rule: did not agree with the other first cycle
  Tilted,      // the beacon was tilted far from its rest direction (picked up, flipped)
};

const char* moveVerdictName(MoveVerdict v);  // "count", "fast", "noreturn", ...
bool moveCounted(MoveVerdict v);

struct MoveInfo {
  uint32_t dipMs;      // when the rep counter counted the dip (millis)
  MoveVerdict verdict;
  int16_t riseMm;      // kUnknownMm if unknown (the session started mid-lift)
  int16_t returnMm;    // kUnknownMm if not measured
  uint8_t tiltDeg;     // largest tilt during its lift
};

class RepValidator {
 public:
  static const size_t kMaxMoves = 100;  // dips per set (the rep counter stops at 99)
  static const int16_t kUnknownMm = -32768;

  explicit RepValidator(const RepValidatorConfig& cfg = RepValidatorConfig());
  void setConfig(const RepValidatorConfig& cfg) { cfg_ = cfg; }
  const RepValidatorConfig& config() const { return cfg_; }

  // Starts a session. restMg: the accelerometer at rest (gravity; its direction is the tilt
  // reference, its length g0). anchored: the stack is at rest now (lift-start wake); false when the
  // session starts at a dip, mid-motion, so the first stroke's velocity is unknown.
  void begin(uint32_t nowMs, const int16_t restMg[3], bool anchored);
  void addSample(uint32_t nowMs, const int16_t accMg[3]);
  void onCandidate(uint32_t nowMs);  // the rep counter counted a dip
  // Ends the session: open moves are decided with what is known (a lone first cycle counts).
  void endSession(uint32_t nowMs);
  bool active() const { return active_; }
  bool anchored() const { return anchored_; }

  uint16_t published() const { return static_cast<uint16_t>(counted_ + unchecked_); }
  uint16_t ignored() const;  // moves rejected so far
  // Dip times of newly counted reps, oldest first; one per call. Reps can be counted late (the
  // first two together), so the weight recorder takes each rep's time from here.
  bool popCounted(uint32_t* dipMs);

  size_t moveCount() const { return moveN_; }
  MoveInfo move(size_t i) const;

  // Live values for plotting (Teleplot): stack position (mm) and leaky velocity (m/s).
  float positionMm() const { return X_ + preMm_ + xr_ * 1000.0f; }
  float velocity() const { return vhp_; }

 private:
  enum Stage : uint8_t { kRise, kReturn, kDone };
  enum TurnKind : uint8_t { kStart, kPeak, kValley };
  struct Stroke {
    int32_t t0, t1;  // session ms
    float d;         // displacement, mm
    float dedriftS;  // the part de-drifted linearly (after a stop, if it had one), s
    bool fromRest;   // that part started at rest (a stop, or the wake)
    int8_t dir;
    float minCos;    // cos of the largest tilt during it
  };
  struct Turn {
    int32_t t;
    float x;
    TurnKind kind;
  };
  struct Move {
    int32_t t;
    uint32_t dipMs;
    MoveVerdict verdict;
    Stage stage;
    bool start;      // the dip that started an unanchored session
    bool hasPeak;
    int32_t stroke;  // owning up-stroke (absolute index)
    int32_t tPeak;
    float rise, ret, peak, amp;
    float minCos;
  };
  static const int32_t kStrokeRing = 32;
  static const int32_t kTurnRing = 32;
  static const size_t kAmpWindow = 5;

  int32_t rel(uint32_t ms) const { return static_cast<int32_t>(ms - t0Ms_); }
  const Stroke& stroke(int32_t k) const { return strokes_[k % kStrokeRing]; }
  const Turn& turn(int32_t k) const { return turns_[k % kTurnRing]; }
  int32_t oldestTurn() const { return turnN_ > kTurnRing ? turnN_ - kTurnRing : 0; }
  void endStroke(int32_t tEnd, float vr, float xr, float T);
  void checkStop(int32_t t, float a);
  void zigzag(int32_t t, float x, bool final);
  void addTurn(int32_t t, float x, TurnKind kind);
  void resolve(int32_t t, bool final);
  void findPeak(Move& m, bool final);
  void findReturn(Move& m, int32_t t, bool final);
  void decide(Move& m);
  void reject(Move& m, MoveVerdict why);
  void count(Move& m, MoveVerdict how);
  bool liftCounted(const Move& m) const;
  void tryFast(Move& m);
  float need() const;
  float swingBase() const;
  void pushAmp(float a);

  RepValidatorConfig cfg_;
  bool active_ = false;
  bool anchored_ = false;
  uint32_t t0Ms_ = 0;
  int32_t tPrev_ = 0;
  bool havePrev_ = false;
  float g0_ = 1000.0f;
  float gref_[3] = {0, 0, 1};
  float dlp_[3] = {0, 0, 1000};
  float alp_ = 0, bias_ = 0, vhp_ = 0;
  int8_t dir_ = 0;
  float vr_ = 0, xr_ = 0, T_ = 0;       // current stroke since its start
  float zcV_ = 0, zcX_ = 0, zcT_ = 0;   // since the leaky velocity's last zero crossing
  bool haveSnap_ = false;
  int32_t snapT_ = 0;
  float snapV_ = 0, snapX_ = 0, snapTT_ = 0;
  int32_t strokeStart_ = 0;
  float X_ = 0;                         // position at the end of the last stroke, mm
  float preMm_ = 0;                     // this stroke's displacement before its stop
  bool stopped_ = false;                // this stroke already had its stop
  bool holding_ = false;                // ... and the stack is still held there
  bool quiet_ = false;                  // quiet run in progress, and the stroke at its start:
  int32_t quietT_ = 0;
  float quietV_ = 0, quietX_ = 0, quietTT_ = 0;
  float cosNow_ = 1, strokeMinCos_ = 1;
  Stroke strokes_[kStrokeRing];
  int32_t strokeN_ = 0;
  Turn turns_[kTurnRing];
  int32_t turnN_ = 0;
  int8_t zmode_ = 0;
  int32_t zhiT_ = 0, zloT_ = 0;
  float zhiX_ = 0, zloX_ = 0;
  bool zhiProv_ = false, zloProv_ = false;
  Move moves_[kMaxMoves];
  size_t moveN_ = 0;
  int prov_ = -1;                       // index of the held first cycle
  uint16_t counted_ = 0, unchecked_ = 0;
  float amps_[kAmpWindow];
  size_t ampN_ = 0;
  uint32_t countedDip_[kMaxMoves];
  size_t countedN_ = 0, countedRead_ = 0;
};

}  // namespace gym
