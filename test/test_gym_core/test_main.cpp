// On-device unit tests for lib/gym_core (protocol, rep counter, machine tracker).
// Run on either board:  pio test -e gateway   (or -e beacon). This replaces the firmware on the
// board; upload the normal firmware again afterwards.
#include <Arduino.h>
#include <unity.h>
#if defined(ARDUINO_ARCH_NRF52)
#include <Adafruit_TinyUSB.h>  // Feather: Serial is USB; this lets PlatformIO find the library
#endif

#include "gym_protocol.h"
#include "machine_tracker.h"
#include "rep_counter.h"

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
  UNITY_END();
}

void loop() {}
