#include "rep_validator.h"

#include <math.h>

namespace gym {

namespace {
const float kG = 9.80665f;
const float kTwoPi = 6.2831853f;
const float kRadToDeg = 57.29578f;

float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

int16_t toMm(float v) {
  if (isnan(v)) return RepValidator::kUnknownMm;
  return static_cast<int16_t>(lroundf(clampf(v, -32000.0f, 32000.0f)));
}

bool rejected(MoveVerdict v) {
  return v == MoveVerdict::NoRise || v == MoveVerdict::NoReturn || v == MoveVerdict::Small ||
         v == MoveVerdict::Superseded || v == MoveVerdict::Dropped || v == MoveVerdict::Tilted;
}
}  // namespace

const char* moveVerdictName(MoveVerdict v) {
  switch (v) {
    case MoveVerdict::Pending: return "pending";
    case MoveVerdict::Held: return "held";
    case MoveVerdict::Counted: return "count";
    case MoveVerdict::Fast: return "fast";
    case MoveVerdict::Unchecked: return "unchecked";
    case MoveVerdict::NoRise: return "norise";
    case MoveVerdict::NoReturn: return "noreturn";
    case MoveVerdict::Small: return "small";
    case MoveVerdict::Superseded: return "superseded";
    case MoveVerdict::Dropped: return "dropped";
    case MoveVerdict::Tilted: return "tilted";
  }
  return "?";
}

bool moveCounted(MoveVerdict v) {
  return v == MoveVerdict::Counted || v == MoveVerdict::Fast || v == MoveVerdict::Unchecked;
}

RepValidator::RepValidator(const RepValidatorConfig& cfg) : cfg_(cfg) {}

// ---------------------------------------------------------------- session

void RepValidator::begin(uint32_t nowMs, const int16_t restMg[3], bool anchored) {
  const float g[3] = {static_cast<float>(restMg[0]), static_cast<float>(restMg[1]), static_cast<float>(restMg[2])};
  const float n = sqrtf(g[0] * g[0] + g[1] * g[1] + g[2] * g[2]);
  active_ = true;
  anchored_ = anchored;
  t0Ms_ = nowMs;
  havePrev_ = false;
  g0_ = n > 1.0f ? n : 1000.0f;
  for (int a = 0; a < 3; a++) {
    gref_[a] = n > 1.0f ? g[a] / n : (a == 2 ? 1.0f : 0.0f);
    dlp_[a] = n > 1.0f ? g[a] : gref_[a] * 1000.0f;
  }
  alp_ = bias_ = vhp_ = 0.0f;
  dir_ = 0;
  vr_ = xr_ = T_ = 0.0f;
  zcV_ = zcX_ = zcT_ = 0.0f;
  haveSnap_ = false;
  strokeStart_ = 0;
  X_ = 0.0f;
  preMm_ = 0.0f;
  stopped_ = holding_ = false;
  quiet_ = false;
  cosNow_ = strokeMinCos_ = 1.0f;
  strokeN_ = 0;
  turnN_ = 0;
  addTurn(0, 0.0f, kStart);
  zmode_ = 0;
  zhiT_ = zloT_ = 0;
  zhiX_ = zloX_ = 0.0f;
  zhiProv_ = zloProv_ = false;
  moveN_ = 0;
  prov_ = -1;
  counted_ = unchecked_ = 0;
  ampN_ = 0;
  countedN_ = countedRead_ = 0;
}

void RepValidator::addSample(uint32_t nowMs, const int16_t accMg[3]) {
  if (!active_) return;
  const int32_t t = rel(nowMs);
  float dt = havePrev_ ? (t - tPrev_) * 0.001f : 0.02f;
  dt = clampf(dt, 0.001f, 0.1f);
  const float acc[3] = {static_cast<float>(accMg[0]), static_cast<float>(accMg[1]), static_cast<float>(accMg[2])};
  if (!havePrev_ && anchored_) {
    // Woken at the start of a lift: gravity points where this first sample points (the lift adds
    // only a little along it). It may have moved since the resting reading if the beacon was
    // knocked; its length stays g0.
    const float n = sqrtf(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
    if (n > 500.0f) {
      for (int k = 0; k < 3; k++) {
        gref_[k] = acc[k] / n;
        dlp_[k] = gref_[k] * g0_;
      }
    }
  }
  tPrev_ = t;
  havePrev_ = true;

  // Acceleration along the (low-passed) gravity direction, minus gravity, low-passed.
  const float kd = 1.0f - expf(-kTwoPi * cfg_.dirHz * dt);
  for (int a = 0; a < 3; a++) dlp_[a] += kd * (acc[a] - dlp_[a]);
  const float n = sqrtf(dlp_[0] * dlp_[0] + dlp_[1] * dlp_[1] + dlp_[2] * dlp_[2]);
  float u[3];
  for (int a = 0; a < 3; a++) u[a] = n > 1.0f ? dlp_[a] / n : gref_[a];
  float a = ((acc[0] * u[0] + acc[1] * u[1] + acc[2] * u[2]) - g0_) * kG / 1000.0f;
  alp_ += (1.0f - expf(-kTwoPi * cfg_.lowPassHz * dt)) * (a - alp_);
  a = alp_;

  // Tilt against a reference that follows the gravity direction slowly.
  float rn = 0.0f;
  for (int k = 0; k < 3; k++) {
    gref_[k] += (u[k] - gref_[k]) * (dt / cfg_.tiltRefS);
    rn += gref_[k] * gref_[k];
  }
  rn = sqrtf(rn);
  for (int k = 0; k < 3; k++) gref_[k] /= rn;
  cosNow_ = clampf(u[0] * gref_[0] + u[1] * gref_[1] + u[2] * gref_[2], -1.0f, 1.0f);
  if (cosNow_ < strokeMinCos_) strokeMinCos_ = cosNow_;
  for (size_t i = 0; i < moveN_; i++) {  // undecided moves keep watching: handling may start late
    Move& m = moves_[i];
    if (m.verdict == MoveVerdict::Pending && !m.start && cosNow_ < m.minCos) m.minCos = cosNow_;
  }

  // Direction: a leaky velocity of the bias-corrected acceleration.
  bias_ += (a - bias_) * dt / cfg_.biasS;
  const float prev = vhp_;
  vhp_ = vhp_ * expf(-dt / cfg_.leakS) + (a - bias_) * dt;

  // The stroke's own integration, and the same since the leaky velocity's last zero crossing.
  const float v0 = vr_;
  vr_ += a * dt;
  xr_ += 0.5f * (vr_ + v0) * dt;
  T_ += dt;
  const float z0 = zcV_;
  zcV_ += a * dt;
  zcX_ += 0.5f * (zcV_ + z0) * dt;
  zcT_ += dt;
  checkStop(t, a);
  if ((prev > 0.0f) != (vhp_ > 0.0f)) {
    haveSnap_ = true;
    snapT_ = t;
    snapV_ = vr_;
    snapX_ = xr_;
    snapTT_ = T_;
    zcV_ = zcX_ = zcT_ = 0.0f;
  }
  int8_t want = dir_;
  if (vhp_ > cfg_.switchMps) want = 1;
  if (vhp_ < -cfg_.switchMps) want = -1;
  if (want != dir_) {
    if (dir_ != 0 && haveSnap_) {
      endStroke(snapT_, snapV_, snapX_, snapTT_);
      vr_ = zcV_;
      xr_ = zcX_;
      T_ = zcT_;
      strokeStart_ = snapT_;
      if (quiet_) {  // a quiet run that began before the switch: count it from the new stroke's start
        quietT_ = t;
        quietV_ = vr_;
        quietX_ = xr_;
        quietTT_ = T_;
      }
    }
    dir_ = want;
    haveSnap_ = false;
  }
  // While a stop may be about to be confirmed, the live position still carries the leftover
  // velocity: it must not confirm a turning point yet.
  const bool stopPending = quiet_ && !stopped_ && dir_ != 0 &&
                           quietT_ - strokeStart_ <= static_cast<int32_t>(cfg_.stopWithinMs);
  if (!stopPending) zigzag(t, positionMm(), false);
  resolve(t, false);
}

// The stack stopped soon after a stroke boundary: from the start of the quiet run its velocity is 0,
// and it stays 0 while the run lasts (a hold). What the stroke had gained by the stop is the
// velocity it started with, left over from a boundary that came a little early: the previous
// stroke really ended at that speed (its linear de-drift took it for drift), and this stroke
// started at it. A quiet run later in a stroke is left alone: it may as well be a steady speed.
void RepValidator::checkStop(int32_t t, float a) {
  if (fabsf(a - bias_) >= cfg_.quietMps2) {
    quiet_ = holding_ = false;
    return;
  }
  if (!quiet_) {
    quiet_ = true;
    quietT_ = t;
    quietV_ = vr_;
    quietX_ = xr_;
    quietTT_ = T_;
  }
  if (!holding_) {
    if (stopped_ || dir_ == 0 || t - quietT_ < static_cast<int32_t>(cfg_.quietMs) ||
        quietT_ - strokeStart_ > static_cast<int32_t>(cfg_.stopWithinMs)) {
      return;
    }
    const float Ta = quietTT_;
    const float gained = quietV_ - bias_ * Ta;
    if (fabsf(gained) > cfg_.stopMaxMps) return;
    const float u = -gained;  // the velocity this stroke started with
    float pre = (quietX_ - 0.5f * bias_ * Ta * Ta + u * Ta) * 1000.0f;
    preMm_ += pre;
    stopped_ = holding_ = true;
  }
  // Held still: no velocity, no displacement, and the stroke's integration (and its linear
  // de-drift) restarts at the last still sample. If the direction switches before the leaky
  // velocity crosses zero again, the stroke ends here.
  vr_ = xr_ = T_ = 0.0f;
  zcV_ = zcX_ = zcT_ = 0.0f;
  vhp_ = 0.0f;
  haveSnap_ = true;
  snapT_ = t;
  snapV_ = snapX_ = snapTT_ = 0.0f;
}

void RepValidator::endStroke(int32_t tEnd, float vr, float xr, float T) {
  if (T <= 0.0f && !stopped_) return;
  Stroke& s = strokes_[strokeN_ % kStrokeRing];
  s.t0 = strokeStart_;
  s.t1 = tEnd;
  s.fromRest = stopped_ || (strokeN_ == 0 && anchored_);
  if (T <= 0.0f) {
    s.d = preMm_;
  } else if (s.fromRest) {
    // Started at rest: the velocity it ends with is left over from the early boundary, not drift.
    s.d = preMm_ + (xr - 0.5f * bias_ * T * T) * 1000.0f;
  } else {
    // v = 0 taken at both ends: remove the leftover velocity linearly. Both ends are early
    // boundaries with leftovers of opposite sign, so their errors mostly cancel.
    s.d = preMm_ + (xr - vr * T * 0.5f) * 1000.0f;
  }
  s.dedriftS = T;
  s.dir = dir_;
  s.minCos = strokeMinCos_;
  strokeN_++;
  X_ += s.d;
  strokeMinCos_ = cosNow_;
  vr_ = xr_ = T_ = 0.0f;
  preMm_ = 0.0f;
  stopped_ = holding_ = false;
  zigzag(tEnd, X_, true);
}

void RepValidator::endSession(uint32_t nowMs) {
  if (!active_) return;
  const int32_t t = rel(nowMs);
  if (dir_ != 0 && T_ > 0.0f) endStroke(t, vr_, xr_, T_);  // the open stroke ends now
  resolve(t, true);
  if (prov_ >= 0 && counted_ == 0) count(moves_[prov_], MoveVerdict::Counted);  // a lone first cycle
  prov_ = -1;
  active_ = false;
}

// ---------------------------------------------------------------- turning points

void RepValidator::addTurn(int32_t t, float x, TurnKind kind) {
  Turn& r = turns_[turnN_ % kTurnRing];
  r.t = t;
  r.x = x;
  r.kind = kind;
  turnN_++;
}

// Extremes are stroke-end positions (final); live positions only confirm them. An extreme set from
// a live position at confirmation is provisional and replaced by the next final value.
void RepValidator::zigzag(int32_t t, float x, bool final) {
  const float h = cfg_.turnMm;
  if (final) {
    if (zmode_ >= 0 && (zhiProv_ || x > zhiX_)) {
      zhiT_ = t;
      zhiX_ = x;
      zhiProv_ = false;
    }
    if (zmode_ <= 0 && (zloProv_ || x < zloX_)) {
      zloT_ = t;
      zloX_ = x;
      zloProv_ = false;
    }
  }
  if (zmode_ >= 0 && !zhiProv_ && x < zhiX_ - h) {
    if (zmode_ == 0 && turnN_ == 1 && zhiT_ == turns_[0].t) {
      turns_[0].kind = kPeak;  // the session started at the top
    } else {
      addTurn(zhiT_, zhiX_, kPeak);
    }
    zmode_ = -1;
    zloT_ = t;
    zloX_ = x;
    zloProv_ = !final;
  } else if (zmode_ <= 0 && !zloProv_ && x > zloX_ + h) {
    if (zmode_ == 0 && turnN_ == 1 && zloT_ == turns_[0].t) {
      turns_[0].kind = kValley;  // the session started at the bottom
    } else {
      addTurn(zloT_, zloX_, kValley);
    }
    zmode_ = 1;
    zhiT_ = t;
    zhiX_ = x;
    zhiProv_ = !final;
  }
}

// ---------------------------------------------------------------- moves

void RepValidator::onCandidate(uint32_t nowMs) {
  if (!active_ || moveN_ >= kMaxMoves) return;
  const int32_t t = rel(nowMs);
  Move& m = moves_[moveN_];
  moveN_++;
  m.t = t;
  m.dipMs = nowMs;
  m.verdict = MoveVerdict::Pending;
  m.stage = kRise;
  m.start = false;
  m.hasPeak = false;
  m.stroke = -1;
  m.tPeak = 0;
  m.rise = m.ret = m.peak = m.amp = NAN;
  m.minCos = 1.0f;
  if (!anchored_ && t <= 0) {
    // The dip that started the session: its stroke is not anchored, so it cannot be judged. It
    // counts unchecked once its peak is known, unless a later dip shares that peak.
    m.start = true;
    return;
  }
  const Stroke* last = strokeN_ > 0 ? &stroke(strokeN_ - 1) : nullptr;
  const bool lastJustEnded = last != nullptr && t - last->t1 <= cfg_.lateMs;
  m.minCos = lastJustEnded ? fminf(last->minCos, strokeMinCos_) : strokeMinCos_;
  if (m.minCos < cosf(cfg_.tiltDeg / kRadToDeg)) {
    reject(m, MoveVerdict::Tilted);  // picked up, flipped: whatever the stack did
    return;
  }
  if (dir_ > 0) {
    m.stroke = strokeN_;  // the lift in progress
  } else if (lastJustEnded && last->dir > 0) {
    m.stroke = strokeN_ - 1;  // the lift just ended
  } else {
    m.rise = 0.0f;
    reject(m, MoveVerdict::NoRise);  // the stack is going down (or not moving)
    return;
  }
  // An earlier dip in the same lift loses: the setup merging into rep 1's rise.
  for (size_t i = 0; i + 1 < moveN_; i++) {
    Move& o = moves_[i];
    if (o.stage == kDone || o.start) continue;
    if (o.stroke == m.stroke && o.verdict == MoveVerdict::Pending) reject(o, MoveVerdict::Superseded);
  }
  for (size_t i = 0; i + 1 < moveN_; i++) {
    const Move& o = moves_[i];
    if (o.stroke == m.stroke && moveCounted(o.verdict)) {
      reject(m, MoveVerdict::Superseded);  // this lift already counted
      return;
    }
  }
  tryFast(m);
}

void RepValidator::reject(Move& m, MoveVerdict why) {
  if (prov_ >= 0 && &moves_[prov_] == &m) prov_ = -1;
  m.verdict = why;
  m.stage = kDone;
}

void RepValidator::count(Move& m, MoveVerdict how) {
  if (how == MoveVerdict::Unchecked) {
    unchecked_++;
  } else {
    counted_++;
  }
  m.verdict = how;
  if (countedN_ < kMaxMoves) countedDip_[countedN_++] = m.dipMs;
}

void RepValidator::pushAmp(float a) {
  if (isnan(a)) return;
  amps_[ampN_ % kAmpWindow] = a;
  ampN_++;
}

float RepValidator::need() const {
  float n = cfg_.minTravelMm;
  if (counted_ >= 2 && ampN_ > 0) {
    float v[kAmpWindow];
    const size_t k = ampN_ < kAmpWindow ? ampN_ : kAmpWindow;
    for (size_t i = 0; i < k; i++) v[i] = amps_[i];
    for (size_t i = 1; i < k; i++) {  // insertion sort
      const float x = v[i];
      size_t j = i;
      while (j > 0 && v[j - 1] > x) {
        v[j] = v[j - 1];
        j--;
      }
      v[j] = x;
    }
    const float med = (k % 2) ? v[k / 2] : 0.5f * (v[k / 2 - 1] + v[k / 2]);
    const float m = cfg_.matchPct / 100.0f * med;
    if (m > n) n = m;
  }
  return n;
}

// Lowest point of the rise in progress: the last valley, or the running low while still falling.
float RepValidator::swingBase() const {
  if (zmode_ == 1) {
    for (int32_t k = turnN_ - 1; k >= oldestTurn(); k--) {
      if (turn(k).kind != kPeak) return turn(k).x;
    }
  }
  return zloX_;
}

void RepValidator::tryFast(Move& m) {
  if (counted_ < 2) return;
  if (positionMm() - swingBase() >= need()) count(m, MoveVerdict::Fast);
}

void RepValidator::resolve(int32_t t, bool final) {
  // The unanchored start dip last, so a later dip that shares its peak is seen first.
  for (int pass = 0; pass < 2; pass++) {
    for (size_t i = 0; i < moveN_; i++) {
      Move& m = moves_[i];
      if (m.start != (pass == 1)) continue;
      if (m.stage == kRise) findPeak(m, final);
      if (m.stage == kReturn) findReturn(m, t, final);
    }
  }
}

void RepValidator::findPeak(Move& m, bool final) {
  if (!m.start && m.stroke >= strokeN_ && !final) return;  // its lift is still going
  const bool own = !m.start && m.stroke < strokeN_ && m.stroke >= strokeN_ - kStrokeRing;
  const int32_t tEnd = own ? stroke(m.stroke).t1 : m.t;
  int32_t pk = -1;
  for (int32_t k = oldestTurn(); k < turnN_; k++) {
    if (turn(k).kind == kPeak && turn(k).t >= tEnd) {
      pk = k;
      break;
    }
  }
  int32_t peakT = 0;
  float peakX = 0.0f;
  int32_t searchFrom = 0;  // turning points before the peak
  if (pk >= 0) {
    peakT = turn(pk).t;
    peakX = turn(pk).x;
    searchFrom = pk - 1;
  } else {
    if (!final) return;
    if (m.start) {
      count(m, MoveVerdict::Unchecked);
      m.stage = kDone;
      return;
    }
    if (zmode_ < 0) {
      m.rise = 0.0f;
      reject(m, MoveVerdict::NoRise);
      return;
    }
    peakT = zhiT_;  // the session ended while rising: tentative peak
    peakX = zhiX_;
    searchFrom = turnN_ - 1;
  }
  int32_t vp = -1;  // the valley (or start) before the peak
  for (int32_t k = searchFrom; k >= oldestTurn(); k--) {
    if (turn(k).kind != kPeak) {
      vp = k;
      break;
    }
  }
  if (vp >= 0 && own && turn(vp).t > stroke(m.stroke).t0 + 50) {
    m.rise = 0.0f;
    reject(m, MoveVerdict::NoRise);  // its lift was a blip inside a fall
    return;
  }
  m.peak = peakX;
  m.tPeak = peakT;
  m.hasPeak = true;
  // One count per rise: a later dip with the same peak supersedes an earlier undecided one.
  for (size_t i = 0; i < moveN_; i++) {
    Move& o = moves_[i];
    if (&o == &m || !o.hasPeak || o.tPeak != peakT) continue;
    if (m.start && !rejected(o.verdict)) {
      reject(m, MoveVerdict::Superseded);
      return;
    }
    if (o.verdict == MoveVerdict::Pending || o.verdict == MoveVerdict::Held) {
      reject(o, MoveVerdict::Superseded);
    } else if (moveCounted(o.verdict) && m.verdict == MoveVerdict::Pending) {
      reject(m, MoveVerdict::Superseded);
      return;
    }
  }
  if (m.start) {
    count(m, MoveVerdict::Unchecked);
    m.stage = kDone;
    return;
  }
  const bool unknownBase = vp < 0 || (!anchored_ && turn(vp).t <= 0);
  m.rise = unknownBase ? NAN : peakX - turn(vp).x;
  m.stage = kReturn;
}

void RepValidator::findReturn(Move& m, int32_t t, bool final) {
  float valley = 0.0f;
  bool found = false;
  for (int32_t k = oldestTurn(); k < turnN_; k++) {
    if (turn(k).kind == kValley && turn(k).t > m.tPeak) {
      valley = turn(k).x;
      found = true;
      break;
    }
  }
  if (!found) {
    if (!final) return;
    valley = fminf(zmode_ <= 0 ? zloX_ : m.peak, X_);
  }
  (void)t;
  m.ret = m.peak - valley;
  m.stage = kDone;
  decide(m);
}

void RepValidator::decide(Move& m) {
  m.amp = isnan(m.rise) ? m.ret : fminf(m.rise, m.ret);
  if (m.verdict == MoveVerdict::Fast) {
    pushAmp(m.amp);
    return;
  }
  if (m.verdict != MoveVerdict::Pending) return;
  if (m.minCos < cosf(cfg_.tiltDeg / kRadToDeg)) {
    m.verdict = MoveVerdict::Tilted;  // handled before its cycle was complete
    return;
  }
  const float n = need();
  if (m.ret < n && (isnan(m.rise) || m.rise >= n)) {
    m.verdict = MoveVerdict::NoReturn;
    return;
  }
  if (!isnan(m.rise) && m.rise < n) {
    m.verdict = m.rise < cfg_.turnMm ? MoveVerdict::NoRise : MoveVerdict::Small;
    return;
  }
  if (counted_ == 0) {  // start rule: hold the first cycle until a second one agrees with it
    if (prov_ < 0) {
      prov_ = static_cast<int>(&m - moves_);
      m.verdict = MoveVerdict::Held;
      return;
    }
    Move& p = moves_[prov_];
    const float lo = fminf(p.ret, m.ret);
    const float hi = fmaxf(p.ret, m.ret);
    if (hi > 0.0f && lo / hi >= cfg_.startMatchPct / 100.0f) {
      prov_ = -1;
      count(p, MoveVerdict::Counted);
      count(m, MoveVerdict::Counted);
      pushAmp(p.amp);
      pushAmp(m.amp);
    } else if (m.ret > p.ret) {
      p.verdict = MoveVerdict::Dropped;
      prov_ = static_cast<int>(&m - moves_);
      m.verdict = MoveVerdict::Held;
    } else {
      m.verdict = MoveVerdict::Dropped;
    }
    return;
  }
  count(m, MoveVerdict::Counted);
  pushAmp(m.amp);
}

// ---------------------------------------------------------------- results

uint16_t RepValidator::ignored() const {
  uint16_t n = 0;
  for (size_t i = 0; i < moveN_; i++) {
    if (rejected(moves_[i].verdict)) n++;
  }
  return n;
}

bool RepValidator::popCounted(uint32_t* dipMs) {
  if (countedRead_ >= countedN_) return false;
  if (dipMs != nullptr) *dipMs = countedDip_[countedRead_];
  countedRead_++;
  return true;
}

MoveInfo RepValidator::move(size_t i) const {
  MoveInfo r = {};
  if (i >= moveN_) return r;
  const Move& m = moves_[i];
  r.dipMs = m.dipMs;
  r.verdict = m.verdict;
  r.riseMm = toMm(m.rise);
  r.returnMm = toMm(m.ret);
  r.tiltDeg = static_cast<uint8_t>(lroundf(acosf(clampf(m.minCos, -1.0f, 1.0f)) * kRadToDeg));
  return r;
}

}  // namespace gym
