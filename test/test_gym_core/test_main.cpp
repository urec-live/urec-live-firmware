// On-device unit tests for lib/gym_core (protocol, rep counter, machine tracker, weight pipeline
// and weight model). Run on either board:  pio test -e gateway   (or -e beacon). This replaces the
// firmware on the board; upload the normal firmware again afterwards.
#include <Arduino.h>
#include <math.h>
#include <unity.h>
#if defined(ARDUINO_ARCH_NRF52)
#include <Adafruit_TinyUSB.h>  // Feather: Serial is USB; this lets PlatformIO find the library
#endif

#include "gym_protocol.h"
#include "machine_tracker.h"
#include "rep_counter.h"
#include "weight_model.h"
#include "weight_pipeline.h"

using namespace gym;

void setUp() {}
void tearDown() {}

// ---------------------------------------------------------------- protocol

static BeaconFrame sampleFrame() {
  BeaconFrame f;
  f.equipmentType = kEquipLegCurl;
  f.machineInstance = 1;
  f.setSeq = 42;
  f.reps = 7;
  f.txPower = -59;
  return f;
}

void test_encode_layout() {
  uint8_t d[kIBeaconDataLen];
  encodeIBeacon(sampleFrame(), d);
  TEST_ASSERT_EQUAL_HEX8(0x4C, d[0]);
  TEST_ASSERT_EQUAL_HEX8(0x00, d[1]);
  TEST_ASSERT_EQUAL_HEX8(0x02, d[2]);
  TEST_ASSERT_EQUAL_HEX8(0x15, d[3]);
  TEST_ASSERT_EQUAL_HEX8_ARRAY(kUuidBase, &d[4], 14);
  TEST_ASSERT_EQUAL_HEX8(0x00, d[18]);
  TEST_ASSERT_EQUAL_HEX8(0x01, d[19]);  // type = Leg Curl
  TEST_ASSERT_EQUAL_HEX8(42, d[20]);    // major high = setSeq
  TEST_ASSERT_EQUAL_HEX8(1, d[21]);     // major low = instance
  TEST_ASSERT_EQUAL_HEX8(0x00, d[22]);
  TEST_ASSERT_EQUAL_HEX8(0x07, d[23]);  // minor = reps
  TEST_ASSERT_EQUAL_INT8(-59, (int8_t)d[24]);
  TEST_ASSERT_EQUAL_HEX16(0x2A01, packMajor(42, 1));
}

void test_roundtrip() {
  uint8_t d[kIBeaconDataLen];
  BeaconFrame in = sampleFrame();
  in.reps = 300;  // > 255 exercises both minor bytes
  encodeIBeacon(in, d);
  BeaconFrame out;
  TEST_ASSERT_TRUE(decodeIBeacon(d, sizeof(d), &out));
  TEST_ASSERT_EQUAL_UINT16(in.equipmentType, out.equipmentType);
  TEST_ASSERT_EQUAL_UINT8(in.machineInstance, out.machineInstance);
  TEST_ASSERT_EQUAL_UINT8(in.setSeq, out.setSeq);
  TEST_ASSERT_EQUAL_UINT16(300, out.reps);
  TEST_ASSERT_EQUAL_INT8(in.txPower, out.txPower);
}

void test_decode_rejects_foreign() {
  uint8_t d[kIBeaconDataLen];
  encodeIBeacon(sampleFrame(), d);
  BeaconFrame out;
  TEST_ASSERT_FALSE(decodeIBeacon(d, kIBeaconDataLen - 1, &out));  // wrong length
  d[0] = 0x59;                                                      // Nordic company ID
  TEST_ASSERT_FALSE(decodeIBeacon(d, kIBeaconDataLen, &out));
  encodeIBeacon(sampleFrame(), d);
  d[10] ^= 0xFF;  // someone else's iBeacon UUID
  TEST_ASSERT_FALSE(decodeIBeacon(d, kIBeaconDataLen, &out));
  TEST_ASSERT_FALSE(decodeIBeacon(nullptr, kIBeaconDataLen, &out));
}

void test_find_in_payload() {
  uint8_t p[31];
  p[0] = 0x02;  // flags AD
  p[1] = 0x01;
  p[2] = 0x06;
  p[3] = 1 + kIBeaconDataLen;  // manufacturer AD
  p[4] = kAdTypeManufacturerData;
  encodeIBeacon(sampleFrame(), &p[5]);
  BeaconFrame out;
  TEST_ASSERT_TRUE(findIBeaconInPayload(p, 30, &out));
  TEST_ASSERT_EQUAL_UINT16(7, out.reps);
  TEST_ASSERT_FALSE(findIBeaconInPayload(p, 29, &out));  // truncated AD
  p[0] = 0x00;                                           // zero padding first: nothing to find
  TEST_ASSERT_FALSE(findIBeaconInPayload(p, 30, &out));
}

void test_next_set_seq() {
  TEST_ASSERT_EQUAL_UINT8(1, nextSetSeq(0));
  TEST_ASSERT_EQUAL_UINT8(2, nextSetSeq(1));
  TEST_ASSERT_EQUAL_UINT8(1, nextSetSeq(255));
}

// ---------------------------------------------------------------- rep counter

static RepCounterConfig repCfg() {
  RepCounterConfig c;
  c.minRepIntervalMs = 600;
  c.holdoffMs = 350;
  c.setTimeoutMs = 5000;
  c.maxRepsPerSet = 99;
  return c;
}

void test_counter_normal_reps() {
  RepCounter rc(repCfg());
  rc.seedSetSeq(10);
  TEST_ASSERT_TRUE(rc.onCountEvent(1000));
  TEST_ASSERT_EQUAL_UINT16(1, rc.reps());
  TEST_ASSERT_EQUAL_UINT8(11, rc.setSeq());
  TEST_ASSERT_TRUE(rc.phase() == RepPhase::Rearm);
  rc.onRearmEvent(2000);
  TEST_ASSERT_TRUE(rc.phase() == RepPhase::Holdoff);
  rc.poll(2349);
  TEST_ASSERT_TRUE(rc.phase() == RepPhase::Holdoff);
  rc.poll(2350);
  TEST_ASSERT_TRUE(rc.phase() == RepPhase::Count);
  TEST_ASSERT_TRUE(rc.onCountEvent(3000));
  TEST_ASSERT_EQUAL_UINT16(2, rc.reps());
  TEST_ASSERT_EQUAL_UINT8(11, rc.setSeq());
}

void test_counter_pause_at_top_counts_once() {
  RepCounter rc(repCfg());
  TEST_ASSERT_TRUE(rc.onCountEvent(1000));  // deceleration before the pause
  TEST_ASSERT_FALSE(rc.onCountEvent(2500)); // start of the descent after the pause: no re-arm yet
  TEST_ASSERT_EQUAL_UINT16(1, rc.reps());
}

void test_counter_bounce_in_holdoff_ignored() {
  RepCounter rc(repCfg());
  TEST_ASSERT_TRUE(rc.onCountEvent(1000));
  rc.onRearmEvent(2000);                     // stack lands
  TEST_ASSERT_FALSE(rc.onCountEvent(2100));  // bounce while counting is disarmed
  rc.poll(2350);
  TEST_ASSERT_TRUE(rc.onCountEvent(3000));
  TEST_ASSERT_EQUAL_UINT16(2, rc.reps());
}

void test_counter_min_interval() {
  RepCounterConfig c = repCfg();
  c.holdoffMs = 0;
  RepCounter rc(c);
  TEST_ASSERT_TRUE(rc.onCountEvent(1000));
  rc.onRearmEvent(1100);
  TEST_ASSERT_TRUE(rc.phase() == RepPhase::Count);  // no hold-off configured
  TEST_ASSERT_FALSE(rc.onCountEvent(1300));         // 300 ms after the last rep
  TEST_ASSERT_TRUE(rc.phase() == RepPhase::Rearm);
  rc.onRearmEvent(1400);
  TEST_ASSERT_TRUE(rc.onCountEvent(1700));
  TEST_ASSERT_EQUAL_UINT16(2, rc.reps());
}

void test_counter_slow_tempo_stays_one_set() {
  RepCounter rc(repCfg());
  TEST_ASSERT_TRUE(rc.onCountEvent(1000));
  rc.onRearmEvent(4000);  // 3 s later: re-arm event refreshes the set timer
  TEST_ASSERT_FALSE(rc.poll(4350));
  TEST_ASSERT_FALSE(rc.poll(8500));          // 7.5 s after the first rep, 4.5 s after the re-arm
  TEST_ASSERT_TRUE(rc.onCountEvent(8600));   // 7.6 s rep period
  TEST_ASSERT_EQUAL_UINT16(2, rc.reps());
}

void test_counter_timeout_resets() {
  RepCounter rc(repCfg());
  rc.seedSetSeq(1);
  TEST_ASSERT_TRUE(rc.onCountEvent(1000));
  uint32_t deadline = 0;
  TEST_ASSERT_TRUE(rc.nextDeadline(&deadline));
  TEST_ASSERT_EQUAL_UINT32(6000, deadline);
  TEST_ASSERT_FALSE(rc.poll(5999));
  TEST_ASSERT_TRUE(rc.poll(6000));  // ended from the Rearm phase
  TEST_ASSERT_EQUAL_UINT16(0, rc.reps());
  TEST_ASSERT_FALSE(rc.setActive());
  TEST_ASSERT_TRUE(rc.phase() == RepPhase::Count);
  TEST_ASSERT_FALSE(rc.nextDeadline(&deadline));
  TEST_ASSERT_TRUE(rc.onCountEvent(7000));
  TEST_ASSERT_EQUAL_UINT16(1, rc.reps());
  TEST_ASSERT_EQUAL_UINT8(3, rc.setSeq());
}

void test_counter_holdoff_deadline_first() {
  RepCounter rc(repCfg());
  rc.onCountEvent(1000);
  rc.onRearmEvent(2000);
  uint32_t deadline = 0;
  TEST_ASSERT_TRUE(rc.nextDeadline(&deadline));
  TEST_ASSERT_EQUAL_UINT32(2350, deadline);
}

void test_counter_max_reps() {
  RepCounterConfig c = repCfg();
  c.maxRepsPerSet = 3;
  c.holdoffMs = 0;
  RepCounter rc(c);
  uint32_t t = 1000;
  for (int i = 0; i < 5; i++) {
    rc.onCountEvent(t);
    rc.onRearmEvent(t + 100);
    t += 1000;
  }
  TEST_ASSERT_EQUAL_UINT16(3, rc.reps());
}

void test_counter_seq_wrap() {
  RepCounter rc(repCfg());
  rc.seedSetSeq(255);
  rc.onCountEvent(1000);
  TEST_ASSERT_EQUAL_UINT8(1, rc.setSeq());
}

void test_counter_millis_wrap() {
  RepCounter rc(repCfg());
  const uint32_t t0 = 0xFFFFF000u;
  TEST_ASSERT_TRUE(rc.onCountEvent(t0));
  TEST_ASSERT_FALSE(rc.poll(t0 + 4999));  // wraps past 0
  TEST_ASSERT_TRUE(rc.poll(t0 + 5000));
}

// ---------------------------------------------------------------- machine tracker

static TrackerConfig trkCfg() {
  TrackerConfig c;
  c.setGapMs = 8000;
  c.idleMs = 300000;
  c.minRepsForUse = 2;
  return c;
}

void test_tracker_new_set_and_in_use() {
  MachineTracker t(2, trkCfg());
  t.onPacket(0, 5, 1, -60, 1000);
  TEST_ASSERT_TRUE(t.state(0).setOpen);
  TEST_ASSERT_EQUAL_UINT32(1, t.state(0).setNumber);
  TEST_ASSERT_EQUAL_UINT16(1, t.state(0).liveReps());
  TEST_ASSERT_FALSE(t.state(0).inUse);  // one rep could be a bump
  TEST_ASSERT_TRUE(t.takeLiveDirty(0));
  TEST_ASSERT_FALSE(t.takeUseChanged(0));
  t.onPacket(0, 5, 2, -60, 3000);
  TEST_ASSERT_TRUE(t.state(0).inUse);
  TEST_ASSERT_TRUE(t.takeUseChanged(0));
  TEST_ASSERT_FALSE(t.takeLiveDirty(1));  // other machine untouched
}

void test_tracker_duplicates_and_rssi_not_dirty() {
  MachineTracker t(1, trkCfg());
  t.onPacket(0, 5, 3, -60, 1000);
  t.takeLiveDirty(0);
  t.onPacket(0, 5, 3, -60, 1100);  // repeat burst
  TEST_ASSERT_FALSE(t.takeLiveDirty(0));
  t.onPacket(0, 5, 3, -75, 1200);  // RSSI only
  TEST_ASSERT_FALSE(t.takeLiveDirty(0));
  TEST_ASSERT_EQUAL_INT8(-75, t.state(0).rssi);
}

void test_tracker_seq_change_starts_new_set() {
  MachineTracker t(1, trkCfg());
  t.onPacket(0, 5, 3, -60, 1000);
  t.onPacket(0, 6, 1, -60, 2000);
  TEST_ASSERT_EQUAL_UINT32(2, t.state(0).setNumber);
  TEST_ASSERT_EQUAL_UINT16(1, t.state(0).liveReps());
  SetRecord r;
  TEST_ASSERT_TRUE(t.popSet(&r));
  TEST_ASSERT_EQUAL_UINT16(3, r.reps);
  TEST_ASSERT_EQUAL_UINT32(1, r.setNumber);
  TEST_ASSERT_EQUAL_UINT8(5, r.setSeq);
  TEST_ASSERT_EQUAL_UINT32(1000, r.endMs);  // its own last packet, not the next set's first
  TEST_ASSERT_FALSE(t.popSet(&r));
}

void test_tracker_reps_decrease_starts_new_set() {
  MachineTracker t(1, trkCfg());
  t.onPacket(0, 5, 4, -60, 1000);
  t.onPacket(0, 5, 1, -60, 2000);  // rebooted beacon that happened to reuse the sequence number
  TEST_ASSERT_EQUAL_UINT32(2, t.state(0).setNumber);
  SetRecord r;
  TEST_ASSERT_TRUE(t.popSet(&r));
  TEST_ASSERT_EQUAL_UINT16(4, r.reps);
}

void test_tracker_gap_finalize_then_amend() {
  MachineTracker t(1, trkCfg());
  t.onPacket(0, 5, 3, -60, 1000);
  t.tick(8999);
  TEST_ASSERT_TRUE(t.state(0).setOpen);
  t.tick(9000);
  TEST_ASSERT_FALSE(t.state(0).setOpen);
  TEST_ASSERT_EQUAL_UINT16(0, t.state(0).liveReps());
  TEST_ASSERT_EQUAL_UINT16(3, t.state(0).lastSetReps);
  SetRecord r;
  TEST_ASSERT_TRUE(t.popSet(&r));
  TEST_ASSERT_EQUAL_UINT16(3, r.reps);
  t.onPacket(0, 5, 4, -60, 9500);  // late packet of the same set
  TEST_ASSERT_TRUE(t.state(0).setOpen);
  TEST_ASSERT_EQUAL_UINT32(1, t.state(0).setNumber);  // amended, not a new set
  t.tick(17500);
  TEST_ASSERT_TRUE(t.popSet(&r));
  TEST_ASSERT_EQUAL_UINT16(4, r.reps);
  TEST_ASSERT_EQUAL_UINT32(1, r.setNumber);  // same key -> idempotent overwrite
}

void test_tracker_bump_not_recorded() {
  MachineTracker t(1, trkCfg());
  t.onPacket(0, 7, 1, -60, 1000);
  t.tick(9000);
  SetRecord r;
  TEST_ASSERT_FALSE(t.popSet(&r));
  TEST_ASSERT_FALSE(t.state(0).inUse);
  TEST_ASSERT_EQUAL_UINT16(0, t.state(0).lastSetReps);
}

void test_tracker_idle_releases_use() {
  MachineTracker t(1, trkCfg());
  t.onPacket(0, 5, 2, -60, 1000);
  t.takeUseChanged(0);
  t.tick(300999);
  TEST_ASSERT_TRUE(t.state(0).inUse);
  t.tick(301000);
  TEST_ASSERT_FALSE(t.state(0).inUse);
  TEST_ASSERT_TRUE(t.takeUseChanged(0));
}

void test_tracker_ring_overflow() {
  MachineTracker t(1, trkCfg());
  uint32_t now = 1000;
  for (unsigned i = 0; i < MachineTracker::kRingSize + 1; i++) {
    t.onPacket(0, static_cast<uint8_t>(i + 1), 2, -60, now);
    now += 1000;
  }
  t.tick(now + 10000);  // finalizes the last one: 17 records into a 16-slot ring
  TEST_ASSERT_EQUAL(1, static_cast<int>(t.droppedSets()));
  SetRecord r;
  TEST_ASSERT_TRUE(t.popSet(&r));
  TEST_ASSERT_EQUAL_UINT32(2, r.setNumber);  // the oldest was dropped
}

void test_tracker_ignores_bad_index_and_zero_reps() {
  MachineTracker t(1, trkCfg());
  t.onPacket(5, 1, 3, -60, 1000);
  t.onPacket(0, 1, 0, -60, 1000);
  TEST_ASSERT_FALSE(t.state(0).setOpen);
  TEST_ASSERT_TRUE(t.state(0).seen);
}

// ---------------------------------------------------------------- weight pipeline
//
// Synthetic sets: a displacement profile of the weight stack, turned into what an LIS3DH (4 mg
// steps, 3 mg rms noise) and a magnetometer on the top plate would read. Buffers are static: the
// loop task's stack is only 4 KB on the Feather.

static const size_t kSynMax = 2000;
static uint16_t s_t[kSynMax];
static int16_t s_acc[kSynMax][3];
static int16_t s_mag[kSynMax][3];
static uint8_t s_flags[kSynMax];
static uint16_t s_counts[64];
static float s_aUp[kSynMax];
static float s_x[kSynMax];
static SetData s_set;
static MagData s_magData;
static MotionAnalysis s_motion;
static RepFeatures s_feat[kMaxRepSegments];
static WeightModel s_wm;
static WeightModel s_wm2;
static uint8_t s_io[WeightModel::kMaxSerializedBytes];

struct SynSet {
  int reps = 6;              // counted reps; the first lift happens before the recording starts
  float heightMm = 300.0f;
  float upS = 1.25f;
  float downS = 1.25f;
  float topPauseS = 0.0f;
  float bottomPauseS = 0.0f;
  uint32_t topPauseMask = 0xFFFFFFFFu;     // bit k: rep k holds topPauseS at its top
  uint32_t bottomPauseMask = 0xFFFFFFFFu;  // bit k: bottomPauseS at the bottom after rep k
  uint32_t halfRepMask = 0;                // bit k: rep k only goes half as high
  float firstUpS = 0.0f;     // > 0: the first lift takes this long (often slower)
  float heightJitterMm = 0.0f;  // each rep's height varies by up to +-this (deterministic)
  bool minJerk = false;      // minimum-jerk strokes, else raised-cosine halves (a pure sine)
  float startDelayS = 0.0f;  // the recording starts this long after the first count
  float restS = 5.0f;        // stack at rest after landing (the counter's set timeout)
  float tiltRad = 0.0f;
  bool upsideDown = false;
  float biasMg = 0.0f;
  float noiseMg = 3.0f;
  bool dropAtTops = false;   // 8 samples missing around every top
  bool landingRing = false;  // +800 mg spike ringing at 15 Hz when the stack lands
  float magPerMm[3] = {0.4f, -0.3f, -5.0f};  // field change per mm of lift (0.1 uT)
  float magOffset[3] = {2500.0f, -1800.0f, 1200.0f};
  float magNoise[3] = {110.0f, 110.0f, 45.0f};  // TMAG5273 with 4x oversampling
  uint32_t seed = 1;
};

struct Lcg {
  uint32_t s;
  float uniform() {
    s = s * 1664525u + 1013904223u;
    return ((s >> 8) + 0.5f) / 16777216.0f;
  }
  float gauss() { return sqrtf(-2.0f * logf(uniform())) * cosf(6.2831853f * uniform()); }
};

static const float kFirstTopS = 2.0f;  // absolute time of the first top; the recording starts later
static float s_recStartS = 0.0f;       // absolute time of the recording's first sample

static float profile(const SynSet& s, float tau) {
  if (tau <= 0.0f) return 0.0f;
  if (tau >= 1.0f) return 1.0f;
  return s.minJerk ? tau * tau * tau * (10.0f - 15.0f * tau + 6.0f * tau * tau)
                   : 0.5f * (1.0f - cosf(3.14159265f * tau));
}

static float topPause(const SynSet& s, int k) { return (s.topPauseMask >> k) & 1u ? s.topPauseS : 0.0f; }
static float bottomPause(const SynSet& s, int k) { return (s.bottomPauseMask >> k) & 1u ? s.bottomPauseS : 0.0f; }
static float liftTime(const SynSet& s, int k) { return k == 0 && s.firstUpS > 0.0f ? s.firstUpS : s.upS; }
static float repHeight(const SynSet& s, int k) {  // height of the lift that ends at top k
  const float jitter = s.heightJitterMm * (static_cast<float>((k * 37 + 11) % 7) - 3.0f) / 3.0f;
  return ((s.halfRepMask >> k) & 1u ? 0.5f : 1.0f) * s.heightMm + jitter;
}

static float topTime(const SynSet& s, int k) {  // absolute
  float t = kFirstTopS;
  for (int j = 0; j < k; j++) t += topPause(s, j) + s.downS + bottomPause(s, j) + liftTime(s, j + 1);
  return t;
}
static float landingTime(const SynSet& s) { return topTime(s, s.reps - 1) + topPause(s, s.reps - 1) + s.downS; }
static float relTopMs(const SynSet& s, int k) { return (topTime(s, k) - s_recStartS) * 1000.0f; }

// Height above the bottom (mm) at absolute time t: reps start at the top, go down, wait, come back up.
static float position(const SynSet& s, float t) {
  if (t < kFirstTopS) return repHeight(s, 0) * profile(s, (t - kFirstTopS + liftTime(s, 0)) / liftTime(s, 0));
  int k = 0;
  while (k + 1 < s.reps && topTime(s, k + 1) <= t) k++;
  const float h = repHeight(s, k);
  float c = t - topTime(s, k);
  if (c < topPause(s, k)) return h;
  c -= topPause(s, k);
  if (c < s.downS) return h * (1.0f - profile(s, c / s.downS));
  if (k == s.reps - 1) return 0.0f;
  c -= s.downS;
  if (c < bottomPause(s, k)) return 0.0f;
  c -= bottomPause(s, k);
  return repHeight(s, k + 1) * profile(s, c / liftTime(s, k + 1));
}

// Vertical acceleration (mg) from the motion alone.
static float accel(const SynSet& s, float t) {
  const float h = 0.001f;
  return (position(s, t + h) - 2.0f * position(s, t) + position(s, t - h)) / (h * h) / 9.80665f;
}

// When the beacon counts rep k: like the LIS3DH count interrupt in stack mode, |a| below rest -
// 64 mg for 4 samples at 50 Hz, armed from the start of the lift (the two-phase counter ignores
// the previous rep's lowering). Returns a negative time if the dip never gets that deep.
static float countTime(const SynSet& s, int k) {
  const float from = topTime(s, k) - liftTime(s, k);
  int below = 0;
  for (float t = from; t < topTime(s, k) + topPause(s, k) + s.downS; t += 0.02f) {
    below = accel(s, t) < -64.0f ? below + 1 : 0;
    if (below == 4) return t;
  }
  return -1.0f;
}

static void synthesize(const SynSet& s) {
  Lcg rng = {s.seed};
  s_recStartS = countTime(s, 0) + s.startDelayS;  // the recorder starts with the first counted rep
  const float landing = landingTime(s);
  const float up[3] = {0.0f, sinf(s.tiltRad), cosf(s.tiltRad)};
  const float sign = s.upsideDown ? -1.0f : 1.0f;
  size_t n = 0;
  for (int i = 0; s_recStartS + i * 0.02f < landing + s.restS && n < kSynMax; i++) {
    const float t = s_recStartS + i * 0.02f;
    bool drop = false;
    for (int k = 0; s.dropAtTops && k < s.reps; k++) drop = drop || fabsf(t - topTime(s, k)) < 0.085f;
    if (drop) continue;
    float a = accel(s, t);
    if (s.landingRing && t >= landing) {
      a += 800.0f * expf(-(t - landing) / 0.1f) * cosf(6.2831853f * 15.0f * (t - landing));
    }
    const float f = 1000.0f + a + s.biasMg;
    const float x = position(s, t);
    for (uint8_t ax = 0; ax < 3; ax++) {
      const float v = sign * f * up[ax] + s.noiseMg * rng.gauss();
      s_acc[n][ax] = static_cast<int16_t>(lroundf(v / 4.0f) * 4);
      s_mag[n][ax] = static_cast<int16_t>(lroundf(s.magOffset[ax] + s.magPerMm[ax] * x + s.magNoise[ax] * rng.gauss()));
    }
    s_flags[n] = 1;
    s_t[n] = static_cast<uint16_t>(lroundf(i * 20.0f));
    n++;
  }
  size_t c = 0;
  for (int k = 0; k < s.reps; k++) {
    const float tc = countTime(s, k);
    if (tc < 0.0f) continue;
    const float rel = tc - s_recStartS;
    s_counts[c++] = static_cast<uint16_t>(lroundf((rel < 0.0f ? 0.0f : rel) * 1000.0f));
  }
  s_set.tMs = s_t;
  s_set.accMg = s_acc;
  s_set.n = n;
  s_set.countMs = s_counts;
  s_set.countN = c;
  s_set.lastEventMs = static_cast<uint16_t>(lroundf((landing - s_recStartS) * 1000.0f));
  s_magData.b = s_mag;
  s_magData.flags = s_flags;
  s_magData.validBit = 1;
  for (uint8_t ax = 0; ax < 3; ax++) s_magData.noise[ax] = static_cast<int16_t>(lroundf(s.magNoise[ax]));
}

static WeightPipelineConfig w8Cfg() {
  WeightPipelineConfig c;
  c.minRepMs = 2000;
  c.minRepIntervalMs = 600;
  c.stillP2pMg = 48;
  c.lowPassHz = 3.0f;
  return c;
}

static bool runMotion(const SynSet& s) {
  synthesize(s);
  return analyzeMotion(s_set, w8Cfg(), s_aUp, s_x, &s_motion);
}

static void assertHeights(float expectMm, float tolMm) {
  for (size_t k = 0; k < s_motion.segCount; k++) {
    if (s_motion.seg[k].valid) TEST_ASSERT_FLOAT_WITHIN(tolMm, expectMm, s_motion.seg[k].heightMm);
  }
}

void test_w8_sine_upside_down() {
  SynSet s;
  s.upsideDown = true;
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, s_motion.segCount);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, s_motion.validCount);
  assertHeights(300.0f, 20.0f);
  TEST_ASSERT_FLOAT_WITHIN(15.0f, -1000.0f, s_motion.gravityMg[2]);
}

void test_w8_asymmetric_min_jerk_height() {
  SynSet s;  // fast lift, slow lowering: the deepest dip comes before the true top
  s.minJerk = true;
  s.upS = 1.0f;
  s.downS = 1.5f;
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, s_motion.validCount);
  assertHeights(300.0f, 30.0f);
}

void test_w8_accelerometer_bias() {
  SynSet s;
  s.biasMg = 30.0f;
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, s_motion.validCount);
  assertHeights(300.0f, 20.0f);
}

void test_w8_pause_at_top_is_one_rep() {
  SynSet s;  // two equal dips per top, 2.5 s apart, no rise between them
  s.minJerk = true;
  s.reps = 5;
  s.topPauseS = 2.5f;
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, s_motion.segCount);
  assertHeights(300.0f, 40.0f);
}

void test_w8_fast_reps_adapt_dt() {
  SynSet s;  // 1.5 s reps: the paper's dT = 2 s merges them
  s.reps = 8;
  s.upS = 0.75f;
  s.downS = 0.75f;
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  TEST_ASSERT_TRUE(s_motion.adapted);
  TEST_ASSERT_EQUAL_UINT16(900, s_motion.minRepMsUsed);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, s_motion.segCount);
  assertHeights(300.0f, 20.0f);

  s_set.countN = 0;  // without the counted reps: the paper's behaviour, reps merge
  TEST_ASSERT_TRUE(analyzeMotion(s_set, w8Cfg(), s_aUp, s_x, &s_motion));
  TEST_ASSERT_FALSE(s_motion.adapted);
  TEST_ASSERT_TRUE(s_motion.segCount < static_cast<size_t>(s.reps - 1));
}

void test_w8_recording_starts_after_first_top() {
  SynSet s;  // the recording started just past the first top: sample 0 is inside its dip
  s.startDelayS = 0.3f;
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, s_motion.segCount);  // that top still counts
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, s_motion.validCount);
  assertHeights(300.0f, 20.0f);

  s.startDelayS = 0.9f;  // well into the lowering: the first top is lost, without an artefact
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 2, s_motion.segCount);
  TEST_ASSERT_UINT16_WITHIN(300, static_cast<uint16_t>(lroundf(relTopMs(s, 1))), s_t[s_motion.valleys[0]]);
  assertHeights(300.0f, 20.0f);
}

void test_w8_landing_ringing_ignored() {
  SynSet s;
  s.landingRing = true;
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, s_motion.validCount);
  assertHeights(300.0f, 20.0f);
}

void test_w8_samples_missing_at_tops() {
  SynSet s;
  s.dropAtTops = true;
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, s_motion.validCount);
  assertHeights(300.0f, 30.0f);
  int16_t rest[3];
  const char* err = nullptr;
  const size_t n = magFeatures(s_set, s_magData, s_motion, s_feat, kMaxRepSegments, rest, &err);
  TEST_ASSERT_TRUE(n <= s_motion.validCount);  // zenith windows without enough samples are skipped
  if (n == 0) TEST_ASSERT_NOT_NULL(err);
}

void test_w8_tilted_board() {
  SynSet s;
  s.tiltRad = 40.0f * 3.14159265f / 180.0f;
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, s_motion.validCount);
  assertHeights(300.0f, 15.0f);
}

// A hold at the top on some reps only. The integration windows must still start and end at the
// same point of the motion (before this was fixed, heights came out 194-481 mm).
void test_w8_top_pause_on_some_reps() {
  SynSet s;
  s.minJerk = true;
  s.topPauseS = 1.0f;
  s.topPauseMask = 0x0Au;  // reps 1 and 3
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, s_motion.validCount);
  assertHeights(300.0f, 20.0f);
}

void test_w8_bottom_pause_on_some_reps() {
  SynSet s;  // slow lowering and fast lift: the weakest stop at the bottom
  s.minJerk = true;
  s.upS = 0.9f;
  s.downS = 2.0f;
  s.bottomPauseS = 1.0f;
  s.bottomPauseMask = 0x0Au;
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, s_motion.validCount);
  assertHeights(300.0f, 20.0f);
}

void test_w8_slower_first_rep_still_counts() {
  SynSet s;  // its dip is shallower than 60 % of the others, but the rep counter counted it
  s.minJerk = true;
  s.upS = 0.9f;
  s.downS = 2.0f;
  s.firstUpS = 1.25f;
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, s_motion.validCount);
  assertHeights(300.0f, 20.0f);
}

void test_w8_uncounted_half_reps_do_not_merge() {
  SynSet s;  // two half reps, too shallow to count or find: a window across them is dropped
  s.minJerk = true;
  s.halfRepMask = 0x0Cu;  // reps 2 and 3
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  for (size_t k = 0; k < s_motion.segCount; k++) {
    if (s_motion.seg[k].valid) TEST_ASSERT_TRUE(s_motion.seg[k].durationMs < 4000);
  }
  assertHeights(300.0f, 20.0f);
}

// Known limit (docs/weight-sensing.md §4): the windows assume consecutive reps of similar height.
// With +-25 mm of variation from rep to rep the errors stay bounded; this keeps them from growing.
void test_w8_varying_range_of_motion_bounded() {
  SynSet s;
  s.reps = 8;
  s.heightJitterMm = 25.0f;
  s.magNoise[0] = s.magNoise[1] = s.magNoise[2] = 0.0f;
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  TEST_ASSERT_EQUAL_UINT32(s.reps, s_motion.valleyCount);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, s_motion.validCount);
  int16_t rest[3];
  const char* err = nullptr;
  const size_t n = magFeatures(s_set, s_magData, s_motion, s_feat, kMaxRepSegments, rest, &err);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, n);
  for (size_t k = 0; k < s_motion.segCount; k++) {  // segment k is the lift into top k + 1
    // measured when written: heights within 15 mm, the 5 cm field within 3.2 uT
    TEST_ASSERT_FLOAT_WITHIN(25.0f, repHeight(s, static_cast<int>(k) + 1), s_motion.seg[k].heightMm);
    TEST_ASSERT_INT16_WITHIN(50, -250, s_feat[k].f[kFeatLift5Z]);
  }
}

void test_w8_rest_field_and_features() {
  SynSet s;
  s.magNoise[0] = s.magNoise[1] = s.magNoise[2] = 0.0f;
  TEST_ASSERT_TRUE_MESSAGE(runMotion(s), s_motion.error);
  int16_t rest[3];
  const char* err = nullptr;
  const size_t n = magFeatures(s_set, s_magData, s_motion, s_feat, kMaxRepSegments, rest, &err);
  TEST_ASSERT_EQUAL_UINT32(s.reps - 1, n);
  TEST_ASSERT_INT16_WITHIN(2, 2500, rest[0]);
  TEST_ASSERT_INT16_WITHIN(2, 1200, rest[2]);
  for (size_t k = 0; k < n; k++) {
    // At the top the field changed by 300 mm * -5 = -1500; 5 cm up the lift by about 50 * -5.
    TEST_ASSERT_INT16_WITHIN(80, -1500, s_feat[k].f[kFeatZenithZ]);
    TEST_ASSERT_INT16_WITHIN(60, -250, s_feat[k].f[kFeatLift5Z]);
    TEST_ASSERT_INT16_WITHIN(20, 300, s_feat[k].f[kFeatHeight]);
  }
}

void test_w8_rest_must_be_still() {
  SynSet s;
  s.restS = 1.0f;  // the recording ends before the stack has been still for 1.5 s
  TEST_ASSERT_FALSE(runMotion(s));
  TEST_ASSERT_NOT_NULL(s_motion.error);
}

// ---------------------------------------------------------------- weight model

static void tmagFloors(WeightModel* m) {
  // Same floors as the beacon: one-sample noise / sqrt(11) at the zenith, / sqrt(5) at 5 cm.
  const int16_t floors[kWeightFeatureCount] = {33, 33, 14, 49, 49, 20, 15};
  m->setFloors(floors);
}

static void unitFloors(WeightModel* m, int16_t v) {
  int16_t floors[kWeightFeatureCount];
  for (uint8_t f = 0; f < kWeightFeatureCount; f++) floors[f] = v;
  m->setFloors(floors);
}

static RepFeatures feat(int16_t v) {
  RepFeatures r;
  for (uint8_t f = 0; f < kWeightFeatureCount; f++) r.f[f] = v;
  return r;
}

// Features of one synthetic set (the full pipeline).
static size_t setFeatures(const SynSet& s) {
  if (!runMotion(s)) return 0;
  int16_t rest[3];
  const char* err = nullptr;
  return magFeatures(s_set, s_magData, s_motion, s_feat, kMaxRepSegments, rest, &err);
}

// Field change per mm of lift for weight index k: steps like the paper's Fig. 8a (lats),
// large for light weights and small for heavy ones.
static void weightField(SynSet* s, int k) {
  static const float kZ[4] = {-5.0f, -6.333f, -7.667f, -8.167f};
  s->magPerMm[0] = 0.4f * (k + 1);
  s->magPerMm[1] = -0.3f * (k + 1);
  s->magPerMm[2] = kZ[k];
}

void test_w8_recovers_weight_despite_offset_shift() {
  s_wm.clear();
  tmagFloors(&s_wm);
  for (int k = 0; k < 4; k++) {
    SynSet s;
    weightField(&s, k);
    s.seed = 100 + k;
    const size_t n = setFeatures(s);
    TEST_ASSERT_TRUE(n >= 4);
    TEST_ASSERT_EQUAL_UINT32(n, s_wm.add(static_cast<uint16_t>(100 * (k + 1)), s_feat, n));
  }
  for (int k = 0; k < 4; k++) {
    SynSet s;  // another day: other noise, and the sensor's offset drifted by 15 uT
    weightField(&s, k);
    s.seed = 200 + k;
    for (uint8_t a = 0; a < 3; a++) s.magOffset[a] += 150.0f;
    const size_t n = setFeatures(s);
    TEST_ASSERT_TRUE(n >= 4);
    const WeightEstimate e = s_wm.classify(s_feat, n);
    TEST_ASSERT_TRUE(e.decided);
    TEST_ASSERT_EQUAL_UINT16(100 * (k + 1), e.label);
  }
}

void test_w8_height_weight_ambiguity_never_confidently_wrong() {
  // Paper §5.2.2: a heavier weight lifted lower can produce the same field at the top as a lighter
  // one lifted higher. Here only Z carries the weight, and 187.5 mm of the heavy weight (-8/mm)
  // gives the same zenith change as 300 mm of the light one (-5/mm).
  s_wm.clear();
  tmagFloors(&s_wm);
  SynSet light;
  light.magPerMm[0] = light.magPerMm[1] = 0.0f;
  light.magPerMm[2] = -5.0f;
  light.seed = 300;
  size_t n = setFeatures(light);
  s_wm.add(300, s_feat, n);
  SynSet heavy = light;
  heavy.magPerMm[2] = -8.0f;
  heavy.seed = 301;
  n = setFeatures(heavy);
  s_wm.add(400, s_feat, n);

  SynSet test = heavy;
  test.heightMm = 187.5f;
  test.seed = 302;
  n = setFeatures(test);
  TEST_ASSERT_TRUE(n >= 4);
  const WeightEstimate e = s_wm.classify(s_feat, n);
  TEST_ASSERT_TRUE(!e.decided || e.label == 400);
}

void test_w8_model_single_weight_far_set_unknown() {
  s_wm.clear();
  unitFloors(&s_wm, 10);
  RepFeatures ex[6];
  for (int i = 0; i < 6; i++) ex[i] = feat(static_cast<int16_t>(100 + 2 * i));
  s_wm.add(500, ex, 6);
  RepFeatures reps[5];
  for (int i = 0; i < 5; i++) reps[i] = feat(2000);
  WeightEstimate e = s_wm.classify(reps, 5);
  TEST_ASSERT_FALSE(e.decided);
  TEST_ASSERT_EQUAL_UINT8(5, e.unknown);
  for (int i = 0; i < 5; i++) reps[i] = feat(103);
  e = s_wm.classify(reps, 5);
  TEST_ASSERT_TRUE(e.decided);
  TEST_ASSERT_EQUAL_UINT16(500, e.label);
}

void test_w8_model_one_example_per_weight() {
  s_wm.clear();
  unitFloors(&s_wm, 10);
  RepFeatures a = feat(0);
  RepFeatures b = feat(1000);
  s_wm.add(100, &a, 1);
  s_wm.add(200, &b, 1);
  TEST_ASSERT_EQUAL_UINT16(100, s_wm.vote(feat(50)));
  TEST_ASSERT_EQUAL_UINT16(200, s_wm.vote(feat(950)));
}

void test_w8_model_three_way_tie_undecided() {
  s_wm.clear();
  unitFloors(&s_wm, 10);
  RepFeatures ex[3] = {feat(0), feat(1000), feat(2000)};
  s_wm.add(100, &ex[0], 1);
  s_wm.add(200, &ex[1], 1);
  s_wm.add(300, &ex[2], 1);
  const WeightEstimate e = s_wm.classify(ex, 3);  // one vote each
  TEST_ASSERT_FALSE(e.decided);
  TEST_ASSERT_EQUAL_UINT8(1, e.agree);
}

void test_w8_model_zero_spread_and_two_reps() {
  s_wm.clear();
  unitFloors(&s_wm, 0);  // floors below 1 are raised to 1
  RepFeatures same7[5];
  RepFeatures same9[5];
  for (int i = 0; i < 5; i++) {
    same7[i] = feat(7);
    same9[i] = feat(9);
  }
  s_wm.add(100, same7, 5);  // no spread at all within a weight
  s_wm.add(200, same9, 5);
  TEST_ASSERT_EQUAL_UINT16(100, s_wm.vote(feat(7)));
  TEST_ASSERT_EQUAL_UINT16(200, s_wm.vote(feat(9)));
  WeightEstimate e = s_wm.classify(same7, 3);
  TEST_ASSERT_TRUE(e.decided);
  TEST_ASSERT_EQUAL_UINT16(100, e.label);
  e = s_wm.classify(same7, 2);  // two reps agree, but two votes are too few
  TEST_ASSERT_FALSE(e.decided);
  TEST_ASSERT_EQUAL_UINT8(2, e.agree);
}

void test_w8_model_serialize_roundtrip() {
  s_wm.clear();
  unitFloors(&s_wm, 10);
  RepFeatures ex[5];
  for (int i = 0; i < 5; i++) ex[i] = feat(static_cast<int16_t>(-300 + i));
  s_wm.add(125, ex, 5);
  for (int i = 0; i < 5; i++) ex[i] = feat(static_cast<int16_t>(400 + i));
  s_wm.add(250, ex, 5);
  const size_t n = s_wm.serialize(1, s_io, sizeof(s_io));
  TEST_ASSERT_EQUAL_UINT32(WeightModel::kHeaderBytes + 10 * WeightModel::kExampleBytes, n);
  unitFloors(&s_wm2, 10);
  TEST_ASSERT_TRUE(s_wm2.deserialize(1, s_io, n));
  TEST_ASSERT_EQUAL_UINT32(10, s_wm2.size());
  TEST_ASSERT_EQUAL_UINT16(125, s_wm2.vote(feat(-298)));
  TEST_ASSERT_EQUAL_UINT16(250, s_wm2.vote(feat(401)));
  TEST_ASSERT_FALSE(s_wm2.deserialize(2, s_io, n));  // another sensor's file
  TEST_ASSERT_EQUAL_UINT32(0, s_wm2.size());
  s_io[5] ^= 0xFF;  // another feature layout
  TEST_ASSERT_FALSE(s_wm2.deserialize(1, s_io, n));
  TEST_ASSERT_EQUAL_UINT32(0, s_wm2.size());
  TEST_ASSERT_TRUE(WeightModel::kMaxSerializedBytes <= 4096);
}

void test_w8_model_full_evicts_most_taught() {
  s_wm.clear();
  unitFloors(&s_wm, 10);
  for (int i = 0; i < 15; i++) {
    RepFeatures ex[10];
    for (int j = 0; j < 10; j++) ex[j] = feat(static_cast<int16_t>(i * 10 + j));
    s_wm.add(100, ex, 10);
  }
  RepFeatures other[20];
  for (int j = 0; j < 20; j++) other[j] = feat(static_cast<int16_t>(5000 + j));
  TEST_ASSERT_EQUAL_UINT32(20, s_wm.add(200, other, 20));
  TEST_ASSERT_EQUAL_UINT32(WeightModel::kMaxExamples, s_wm.size());
  uint16_t ls[4];
  uint16_t cs[4];
  TEST_ASSERT_EQUAL_UINT32(2, s_wm.labels(ls, cs, 4));
  TEST_ASSERT_EQUAL_UINT16(140, cs[0]);
  TEST_ASSERT_EQUAL_UINT16(20, cs[1]);
}

void setup() {
  delay(2000);  // let the board's USB serial come up before Unity starts printing
  UNITY_BEGIN();
  RUN_TEST(test_encode_layout);
  RUN_TEST(test_roundtrip);
  RUN_TEST(test_decode_rejects_foreign);
  RUN_TEST(test_find_in_payload);
  RUN_TEST(test_next_set_seq);
  RUN_TEST(test_counter_normal_reps);
  RUN_TEST(test_counter_pause_at_top_counts_once);
  RUN_TEST(test_counter_bounce_in_holdoff_ignored);
  RUN_TEST(test_counter_min_interval);
  RUN_TEST(test_counter_slow_tempo_stays_one_set);
  RUN_TEST(test_counter_timeout_resets);
  RUN_TEST(test_counter_holdoff_deadline_first);
  RUN_TEST(test_counter_max_reps);
  RUN_TEST(test_counter_seq_wrap);
  RUN_TEST(test_counter_millis_wrap);
  RUN_TEST(test_tracker_new_set_and_in_use);
  RUN_TEST(test_tracker_duplicates_and_rssi_not_dirty);
  RUN_TEST(test_tracker_seq_change_starts_new_set);
  RUN_TEST(test_tracker_reps_decrease_starts_new_set);
  RUN_TEST(test_tracker_gap_finalize_then_amend);
  RUN_TEST(test_tracker_bump_not_recorded);
  RUN_TEST(test_tracker_idle_releases_use);
  RUN_TEST(test_tracker_ring_overflow);
  RUN_TEST(test_tracker_ignores_bad_index_and_zero_reps);
  RUN_TEST(test_w8_sine_upside_down);
  RUN_TEST(test_w8_asymmetric_min_jerk_height);
  RUN_TEST(test_w8_accelerometer_bias);
  RUN_TEST(test_w8_pause_at_top_is_one_rep);
  RUN_TEST(test_w8_fast_reps_adapt_dt);
  RUN_TEST(test_w8_recording_starts_after_first_top);
  RUN_TEST(test_w8_landing_ringing_ignored);
  RUN_TEST(test_w8_samples_missing_at_tops);
  RUN_TEST(test_w8_tilted_board);
  RUN_TEST(test_w8_top_pause_on_some_reps);
  RUN_TEST(test_w8_bottom_pause_on_some_reps);
  RUN_TEST(test_w8_slower_first_rep_still_counts);
  RUN_TEST(test_w8_uncounted_half_reps_do_not_merge);
  RUN_TEST(test_w8_varying_range_of_motion_bounded);
  RUN_TEST(test_w8_rest_field_and_features);
  RUN_TEST(test_w8_rest_must_be_still);
  RUN_TEST(test_w8_recovers_weight_despite_offset_shift);
  RUN_TEST(test_w8_height_weight_ambiguity_never_confidently_wrong);
  RUN_TEST(test_w8_model_single_weight_far_set_unknown);
  RUN_TEST(test_w8_model_one_example_per_weight);
  RUN_TEST(test_w8_model_three_way_tie_undecided);
  RUN_TEST(test_w8_model_zero_spread_and_two_reps);
  RUN_TEST(test_w8_model_serialize_roundtrip);
  RUN_TEST(test_w8_model_full_evicts_most_taught);
  UNITY_END();
}

void loop() {}
