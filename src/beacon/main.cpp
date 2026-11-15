// GetMeFit beacon: Feather nRF52840 Express + LIS3DH on the moving part of a gym machine.
//
// Reproduces the beacon of Bian, Rupp & Magno, "Fully Automatic Gym Exercises Recording: An IoT
// Solution" (arXiv 2305.17594): the accelerometer raises a threshold interrupt, each counted
// interrupt is one rep, and the count is broadcast as an iBeacon (UUID = equipment type,
// minor = reps) in short bursts. The count resets after 5 s without motion.
//
// The CPU sleeps in FreeRTOS tickless idle between events; it wakes on the LIS3DH INT1 edge,
// on a timer deadline (hold-off, repeat burst, set timeout) or on the 2 s housekeeping tick.
// docs/rep-counting-walkthrough.md explains every step.
//
// In stack mode a second LIS3DH interrupt wakes the beacon when a lift starts, and the 50 Hz
// sampler then follows the stack (lib/gym_core/src/rep_validator.h): a dip only counts if the stack
// went up and came back down like a rep, so setting up and letting go after the set do not count.
//
// With a magnetometer on the STEMMA QT chain the beacon also weighs each set, reproducing
// W8-Scope (Radhakrishnan, Misra & Balan, 2021): while a set is active it samples at 50 Hz, and
// when the set ends it prints the weight. docs/weight-sensing.md explains it.
//
// The console (commands and everything printed) works on USB and on a phone over Bluetooth
// (ble_console.h). While either is connected, the loop also wakes every 50 ms to read commands.
#include <Arduino.h>
#include <Wire.h>
#include <bluefruit.h>
#include <stdlib.h>
#include <string.h>

#include "battery.h"
#include "beacon_config.h"
#include "ble_console.h"
#include "console_out.h"
#include "gym_protocol.h"
#include "ibeacon_adv.h"
#include "lis3dh_motion.h"
#include "motion_settings.h"
#include "rep_counter.h"
#include "rep_validator.h"
#include "tuning_console.h"
#include "weight_sensing.h"

// Console text for whoever listens: the USB monitor and/or a phone (console_out.h).
#define LOG(...)                                \
  do {                                          \
    if (Console) Console.printf(__VA_ARGS__);   \
  } while (0)

namespace {

const uint32_t kConsoleTickMs = 50;
const uint32_t kDumpTickMs = 5;  // while a CSV dump drains to the USB host
const uint8_t kCalSamples = 32;
const uint8_t kDriftSamples = 8;

gym::RepCounterConfig counterConfig(const MotionSettings& s) {
  gym::RepCounterConfig c;
  c.minRepIntervalMs = s.minRepIntervalMs;
  c.holdoffMs = s.holdoffMs;
  c.setTimeoutMs = BEACON_SET_TIMEOUT_MS;
  c.maxRepsPerSet = BEACON_MAX_REPS_PER_SET;
  return c;
}

gym::RepValidatorConfig validatorConfig(const MotionSettings& s) {
  gym::RepValidatorConfig c;
  c.minTravelMm = s.repMinTravelMm;
  c.matchPct = s.repMatchPct;
  c.startMatchPct = BEACON_REP_START_MATCH_PCT;
  c.turnMm = BEACON_REP_TURN_MM;
  c.tiltDeg = BEACON_REP_TILT_DEG;
  return c;
}

// Wrap-safe "deadline has been reached", for deadlines that are always recent (< 24 days).
bool due(uint32_t now, uint32_t deadline) { return static_cast<int32_t>(now - deadline) >= 0; }

// "duration has passed since start", correct however long the timer sat unused.
bool elapsed(uint32_t now, uint32_t start, uint32_t duration) { return now - start >= duration; }

// FreeRTOS ticks are 1/1024 s. Round up so a wake-up never comes before its deadline, which
// would only make the loop sleep a whole extra interval.
TickType_t ticksCeil(uint32_t ms) {
  return static_cast<TickType_t>((static_cast<uint64_t>(ms) * configTICK_RATE_HZ + 999) / 1000);
}

SemaphoreHandle_t g_intSem = nullptr;
Lis3dhMotion g_imu;
IBeaconAdvertiser g_adv;
WeightSensing g_weight;
MotionSettings g_settings = settingsFromConfig();
gym::RepCounter g_counter(counterConfig(g_settings));
// The rep check (stack mode): follows the stack from the 50 Hz samples during a session, which runs
// from the lift-start wake (or the first dip) to the end of the set, and decides which dips count.
gym::RepValidator g_validator(validatorConfig(g_settings));
int16_t g_restMg[3] = {0, 0, 1000};  // gravity: the latest still reading (calibration, drift check)
bool g_wakeArmed = false;            // LIS3DH interrupt 2 armed for a lift starting
bool g_sessionCheck = false;         // this session counts what the check counts (else: every dip)
bool g_sessionWake = false;          // ... and started at the wake (at rest)
bool g_sessionHadSet = false;        // ... and a dip made it a set
int16_t g_sessionRest[3] = {0, 0, 0};
uint16_t g_published = 0;            // reps broadcast for the set in progress
bool g_moveLogged[gym::RepValidator::kMaxMoves];
Calibration g_cal = {};
PhaseSpec g_countSpec = {};
PhaseSpec g_rearmSpec = {};

// What INT1 is armed for right now; syncArm() makes it follow g_counter.phase().
bool g_hwArmed = false;
gym::RepPhase g_hwPhase = gym::RepPhase::Count;
volatile bool g_serviceNow = false;  // INT1 was found high without an edge: handle it now

gym::BeaconFrame g_lastFrame = {};
bool g_repeatPending = false;
uint32_t g_repeatAtMs = 0;
bool g_advActive = false;  // a burst is on the air until g_advStopAtMs
uint32_t g_advStopAtMs = 0;
uint16_t g_setReps = 0;  // reps of the set in progress (for the end-of-set log)
bool g_statusSent = false;  // the boot status frame has gone out
uint32_t g_lastStatusMs = 0;

uint32_t g_lastHousekeepingMs = 0;
uint32_t g_lastCalAttemptMs = 0;
uint32_t g_lastDriftCheckMs = 0;
bool g_hostConnected = false;  // a serial monitor has the USB port open
bool g_consoleAdvFailed = false;  // logged once, not on every retry

// ---------------------------------------------------------------- interrupt

// Runs in interrupt context (GPIOTE priority 3, so FreeRTOS ...FromISR calls are allowed).
void onInt1Rise() {
  BaseType_t woken = pdFALSE;
  if (g_intSem != nullptr) xSemaphoreGiveFromISR(g_intSem, &woken);
  portYIELD_FROM_ISR(woken);
}

bool usbPowered() {
  uint32_t status = 0;
  if (sd_power_usbregstatus_get(&status) != NRF_SUCCESS) return false;
  return (status & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

uint8_t randomByte() {
  for (int i = 0; i < 10; i++) {
    uint8_t avail = 0;
    uint8_t b = 0;
    sd_rand_application_bytes_available_get(&avail);
    if (avail > 0 && sd_rand_application_vector_get(&b, 1) == NRF_SUCCESS) return b;
    delay(5);
  }
  return static_cast<uint8_t>(micros());
}

void led(bool on) {
#if BEACON_LED_FEEDBACK
  digitalWrite(LED_RED, on ? HIGH : LOW);
#else
  (void)on;
#endif
}

void blinkError() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(LED_RED, HIGH);
    delay(40);
    digitalWrite(LED_RED, LOW);
    delay(80);
  }
}

// Arms LIS3DH interrupt 1 for whatever the counter waits for. Every (re)configuration ends the
// same way: clear the latch, drop the edge it may have produced, and look at the pin, because a
// latched INT1 that is already high never produces another rising edge.
// Interrupt 2, the lift-start wake: armed while no session is active, in stack mode, so the rep
// check sees the stack leave rest. It shares the INT1 pin with interrupt 1.
void syncWake(bool force = false) {
  const bool want = g_cal.valid && g_settings.mode == MotionMode::Stack && !g_counter.active();
  if (!force && want == g_wakeArmed) return;
  if (want) {
    g_imu.armWake(g_cal.axis, true, g_cal.restMg + BEACON_WAKE_DELTA_MG, 2);
  } else {
    g_imu.disarmWake();
  }
  (void)g_imu.readInt2Source();
  g_wakeArmed = want;
  if (digitalRead(BEACON_INT_PIN) == HIGH) g_serviceNow = true;
}

void syncArm(bool force = false) {
  syncWake(force);
  const gym::RepPhase want = g_counter.phase();
  if (!force && g_hwArmed && want == g_hwPhase) return;

  const PhaseSpec* spec = nullptr;
  if (g_cal.valid) {
    if (want == gym::RepPhase::Count) spec = &g_countSpec;
    if (want == gym::RepPhase::Rearm) spec = &g_rearmSpec;
  }
  if (spec != nullptr && spec->valid) {
    g_imu.arm(g_cal.axis, spec->high, spec->thresholdMg, spec->duration);
  } else {
    g_imu.disarm();  // hold-off, or no usable calibration
  }
  (void)g_imu.readInt1Source();
  xSemaphoreTake(g_intSem, 0);
  g_hwPhase = want;
  g_hwArmed = true;
  if (digitalRead(BEACON_INT_PIN) == HIGH) g_serviceNow = true;
}

// ---------------------------------------------------------------- advertising

// Starts a burst and returns; loop() stops it at g_advStopAtMs. Not blocking matters: a rep is
// counted at the top of the lift, exactly where the weight recorder needs its samples.
void advertise(const gym::BeaconFrame& frame, uint32_t now) {
  if (!g_adv.start(frame)) {
    LOG("[err] advertising failed\n");
    return;
  }
  led(true);
  g_advActive = true;
  g_advStopAtMs = now + g_adv.burstMs(BEACON_ADV_EVENTS_PER_BURST);
}

void endBurst() {
  g_adv.stop();
  led(false);
  g_advActive = false;
}

// The Bluetooth console's connectable advertisement fills the gaps between bursts while no phone
// is connected and its window is open (ble_console.h). A burst replaces it; this restarts it.
void syncConsoleAdvertising(uint32_t now) {
  const bool want = ble_console::wantsAdvertising(now) && !g_advActive;
  if (want && !g_adv.consoleOn()) {
    const bool ok = g_adv.startConsole(ble_console::service(), BEACON_BLE_CONSOLE_ADV_INTERVAL_MS);
    if (!ok && !g_consoleAdvFailed) LOG("[err] advertising failed (Bluetooth console)\n");
    g_consoleAdvFailed = !ok;
  } else if (!want && g_adv.consoleOn()) {
    g_adv.stop();
  }
}

void printBattery(uint16_t mv, bool usb) {
  if (usb) {
    Console.printf("%u.%02u V (USB power: the charger's voltage, not the battery's)", mv / 1000, (mv % 1000) / 10);
  } else {
    Console.printf("%u.%02u V (%u %%)", mv / 1000, (mv % 1000) / 10, gym::batteryPercent(mv));
  }
}

// Battery report for the gateway: a status frame at boot, then every BEACON_STATUS_INTERVAL_MS.
// Held back while a set is active or a rep burst is on the air or pending, so it never replaces
// a rep count. No LED: that stays feedback for reps. A rep counted meanwhile replaces it.
void maybeSendStatus(uint32_t now) {
  if (g_statusSent && !elapsed(now, g_lastStatusMs, BEACON_STATUS_INTERVAL_MS)) return;
  if (g_counter.setActive() || g_advActive || g_repeatPending) return;
  g_statusSent = true;
  g_lastStatusMs = now;  // a failed burst waits for the next interval too, so it cannot spam
  gym::StatusFrame frame;
  frame.equipmentType = BEACON_EQUIPMENT_TYPE;
  frame.machineInstance = BEACON_MACHINE_INSTANCE;
  frame.batteryMv = readBatteryMv();  // before the burst: the radio's current would dip it
  frame.flags = usbPowered() ? gym::kStatusUsbPower : 0;
  if (!g_adv.start(frame)) {
    LOG("[err] advertising failed (status)\n");
    return;
  }
  g_advActive = true;
  g_advStopAtMs = now + g_adv.burstMs(BEACON_ADV_EVENTS_PER_BURST);
  if (Console) {
    Console.print(F("[battery] "));
    printBattery(frame.batteryMv, (frame.flags & gym::kStatusUsbPower) != 0);
    Console.println(F(", sent to the gateway"));
  }
}

// ---------------------------------------------------------------- weight sensing

// Weight sensing needs the beacon on the stack; on a lever arm the field says nothing about it.
const char* weightBlocked() {
  return g_settings.mode == MotionMode::Lever ? "lever mode (the beacon must sit on the weight stack)" : nullptr;
}

// Keeps the weight recorder and the rep check in step with the rep counter: they run exactly
// while a session is active (from the lift-start wake, or the first dip, to the set's end). A set
// ended by the 5 s timeout is weighed in onSetEnded(); one ended any other way (cal, reset, mode,
// axis, a drift recalibration) is aborted here, and so is a wake that no dip followed (quietly).
void syncRecorder(uint32_t now) {
  const bool active = g_counter.active();
  if (active && !g_weight.inSet()) g_weight.startSet(now, weightBlocked());
  if (!active && g_weight.inSet()) g_weight.abortSet(now, !g_sessionHadSet);
  if (!active && g_validator.active()) g_validator.endSession(now);
  g_weight.setMotionSampling(g_validator.active(), now);
}

// A session starts: at the wake (the stack at rest, anchored) or at a dip that came without one.
void startSession(uint32_t now, bool wake) {
  g_sessionHadSet = false;
  g_published = 0;
  g_sessionWake = wake;
  g_sessionCheck = g_settings.mode == MotionMode::Stack && g_settings.repCheck;
  memcpy(g_sessionRest, g_restMg, sizeof(g_sessionRest));
  memset(g_moveLogged, 0, sizeof(g_moveLogged));
  if (g_settings.mode == MotionMode::Stack) {
    g_validator.setConfig(validatorConfig(g_settings));
    g_validator.begin(now, g_sessionRest, wake);
  }
  syncRecorder(now);  // the recording starts with the session
}

void broadcastReps(uint32_t now, uint16_t reps) {
  const uint16_t before = g_published;
  g_published = reps;
  g_setReps = reps;
  g_lastFrame.equipmentType = BEACON_EQUIPMENT_TYPE;
  g_lastFrame.machineInstance = BEACON_MACHINE_INSTANCE;
  g_lastFrame.setSeq = g_counter.setSeq();
  g_lastFrame.reps = reps;
  g_lastFrame.txPower = BEACON_RSSI_AT_1M;
  if (reps > before + 1) {
    LOG("[rep] %u  (set seq %u; reps %u-%u counted together once they matched)\n", reps, g_lastFrame.setSeq,
        before + 1, reps);
  } else {
    LOG("[rep] %u  (set seq %u)\n", reps, g_lastFrame.setSeq);
  }
  advertise(g_lastFrame, now);
  // Paper: "double advertisements to decrease the loss rate"; a newer rep replaces this one.
  g_repeatPending = true;
  g_repeatAtMs = now + BEACON_ADV_REPEAT_DELAY_MS;
}

const char* moveReason(gym::MoveVerdict v) {
  switch (v) {
    case gym::MoveVerdict::NoRise: return "no rise (the stack was going down)";
    case gym::MoveVerdict::NoReturn: return "no return";
    case gym::MoveVerdict::Small: return "too small";
    case gym::MoveVerdict::Superseded: return "part of the next lift";
    case gym::MoveVerdict::Dropped: return "did not match the next rep";
    case gym::MoveVerdict::Tilted: return "beacon tilted";
    default: return gym::moveVerdictName(v);
  }
}

void printMm(int16_t mm) {
  if (mm == gym::RepValidator::kUnknownMm) {
    Console.print('?');
  } else {
    Console.print(mm);
  }
}

// One line per move the check rejects, once it is decided.
void logMoves() {
  if (!Console) return;
  for (size_t i = 0; i < g_validator.moveCount(); i++) {
    if (g_moveLogged[i]) continue;
    const gym::MoveInfo m = g_validator.move(i);
    if (m.verdict == gym::MoveVerdict::Pending || m.verdict == gym::MoveVerdict::Held) continue;
    g_moveLogged[i] = true;
    if (gym::moveCounted(m.verdict)) continue;  // its [rep] line says it
    Console.printf("[move] %s: %s", g_sessionCheck ? "ignored" : "would ignore", moveReason(m.verdict));
    if (m.verdict == gym::MoveVerdict::Tilted) {
      Console.printf(" %u deg\n", m.tiltDeg);
    } else if (m.verdict != gym::MoveVerdict::Superseded) {
      Console.print(F(" (up "));
      printMm(m.riseMm);
      Console.print(F(" mm, back "));
      printMm(m.returnMm);
      Console.println(F(" mm)"));
    } else {
      Console.println();
    }
  }
}

// What the check has decided since the last call: the counted reps go to the weight recorder
// (with their dips' times) and, when the check counts, to the gateway.
void publishChecked(uint32_t now) {
  uint32_t dipMs = 0;
  while (g_validator.popCounted(&dipMs)) {
    if (g_sessionCheck) g_weight.onCount(dipMs);
  }
  if (g_sessionCheck && g_validator.published() > g_published) broadcastReps(now, g_validator.published());
  logMoves();
}

// The rep counter counted a dip.
void onDip(uint32_t now) {
  if (!g_counter.active()) return;
  if (!g_weight.inSet() && !g_validator.active()) startSession(now, false);  // no wake came first
  g_sessionHadSet = true;
  if (g_settings.mode == MotionMode::Stack) {
    if (!g_validator.active()) {
      g_validator.setConfig(validatorConfig(g_settings));
      g_validator.begin(now, g_sessionRest, false);
    }
    g_validator.onCandidate(now);
  }
  if (!g_sessionCheck) {  // lever/paper mode, or the check off: every dip counts, as before
    g_weight.onCount(now);
    broadcastReps(now, g_counter.reps());
  }
  publishChecked(now);
}

// ---------------------------------------------------------------- calibration

void deriveSpecs() {
  g_countSpec = countSpec(g_settings, g_cal);
  g_rearmSpec = rearmSpec(g_settings, g_cal);
  if (g_cal.valid && (!g_countSpec.valid || !g_rearmSpec.valid)) {
    LOG("[cal] thresholds out of range (16..2032 mg) - adjust dl/dh\n");
    g_cal.valid = false;
  }
}

// Measures the beacon at rest. Thresholds are relative to this reading, so a beacon that was
// bumped or tilted keeps counting after it recalibrates (the paper lost reps exactly that way).
bool calibrate(uint32_t now) {
  g_counter.endSet();
  syncRecorder(now);  // a set cut short by calibration is not weighed
  g_repeatPending = false;
  g_imu.disarm();
  g_imu.disarmWake();
  g_hwArmed = false;
  g_wakeArmed = false;
  g_lastCalAttemptMs = now;
  g_lastDriftCheckMs = now;

  int16_t mean[3];
  int16_t p2p[3];
  Calibration cal = {};
  const char* why = nullptr;
  // Rewrite the whole sensor configuration first: if the LIS3DH lost power (a loose VIN/GND
  // jumper on a moving stack) it came back powered down, and only this brings it back.
  if (!g_imu.begin(Wire, BEACON_LIS3DH_ADDR)) {
    why = "no LIS3DH on I2C - check SDA/SCL/3V/GND";
  } else if (!g_imu.measure(kCalSamples, mean, p2p)) {
    why = "no data from the LIS3DH";
  } else if (p2p[0] > BEACON_STILL_P2P_MG || p2p[1] > BEACON_STILL_P2P_MG ||
             p2p[2] > BEACON_STILL_P2P_MG) {
    why = "beacon is moving - keep it still";
  } else {
    cal.measured = true;
    cal.axis = chooseAxis(g_settings, mean);
    memcpy(cal.meanMg, mean, sizeof(mean));
    memcpy(g_restMg, mean, sizeof(mean));  // the rep check's gravity
    cal.restMg = static_cast<uint16_t>(abs(mean[cal.axis]));
    why = checkRest(g_settings, cal.restMg);
  }
  cal.valid = why == nullptr;
  g_cal = cal;
  deriveSpecs();

  if (g_cal.valid) {
    LOG("[cal] axis %s, rest %u mg (x %d, y %d, z %d)\n", axisName(g_cal.axis), g_cal.restMg,
        g_cal.meanMg[0], g_cal.meanMg[1], g_cal.meanMg[2]);
  } else {
    LOG("[cal] failed: %s - retrying in %u s\n", why ? why : "thresholds", BEACON_CAL_RETRY_MS / 1000);
  }
  syncArm(true);
  return g_cal.valid;
}

// While idle: if the beacon now rests differently than when it was calibrated, recalibrate.
void checkDrift(uint32_t now) {
  g_lastDriftCheckMs = now;
  if (!g_cal.valid) return;
  int16_t mean[3];
  int16_t p2p[3];
  if (!g_imu.measure(kDriftSamples, mean, p2p)) {
    // No samples: the sensor reset or dropped off the bus. Re-initialise it; if that fails the
    // calibration becomes invalid and housekeeping blinks and retries every 10 s.
    LOG("[err] LIS3DH stopped sampling - reinitialising\n");
    calibrate(now);
    return;
  }
  if (p2p[0] > BEACON_STILL_P2P_MG || p2p[1] > BEACON_STILL_P2P_MG || p2p[2] > BEACON_STILL_P2P_MG) {
    return;  // someone is moving the machine; check again later
  }
  memcpy(g_restMg, mean, sizeof(mean));  // still: the rep check's latest gravity, even without a recalibration
  const int drift = abs(mean[g_cal.axis] - g_cal.meanMg[g_cal.axis]);
  if (drift > BEACON_DRIFT_MG) {
    LOG("[cal] resting reading moved %d mg - recalibrating\n", drift);
    calibrate(now);
  }
}

// ---------------------------------------------------------------- events

void serviceInterrupt(uint32_t now) {
  const uint8_t src = g_imu.readInt1Source();  // also clears the latched pin ...
  const uint8_t src2 = g_imu.readInt2Source();  // ... with this one: both generators drive INT1
  if ((src2 & Lis3dhMotion::kSrcIA) != 0 && g_wakeArmed && !g_counter.active()) {
    g_counter.onWake(now);  // a lift is starting: the session (sampler, recording, check) starts at rest
    startSession(now, true);
  }
  if ((src & Lis3dhMotion::kSrcIA) == 0 || !g_hwArmed) return;

  if (g_hwPhase == gym::RepPhase::Count && g_countSpec.valid &&
      (src & Lis3dhMotion::eventBit(g_cal.axis, g_countSpec.high))) {
    if (g_counter.onCountEvent(now)) {
      onDip(now);
    } else {
      LOG("[int] count event ignored (min interval / max reps)\n");
    }
  } else if (g_hwPhase == gym::RepPhase::Rearm && g_rearmSpec.valid &&
             (src & Lis3dhMotion::eventBit(g_cal.axis, g_rearmSpec.high))) {
    g_counter.onRearmEvent(now);
  }
  syncArm();
}

void onSetEnded(uint32_t now) {
  uint16_t ignored = 0;
  bool lateCount = false;
  if (g_validator.active()) {
    // Decide what is still open (a lone first cycle counts; a last lift with no return does not).
    g_validator.endSession(now);
    const uint16_t before = g_published;
    publishChecked(now);
    lateCount = g_published != before;
    ignored = g_validator.ignored();
    g_weight.setMoves(g_sessionRest, g_sessionWake, g_validator);
  }
  if (g_settings.mode == MotionMode::Stack && ignored > 0) {
    LOG("[set] done: %u reps (%u moves %s)\n", g_setReps, ignored, g_sessionCheck ? "ignored" : "the check would ignore");
  } else {
    LOG("[set] done: %u reps\n", g_setReps);
  }
  g_weight.finishSet(now, g_counter.lastEventMs(), g_counter.setSeq(), g_setReps,
                     static_cast<uint16_t>(g_settings.minRepIntervalMs));
  g_setReps = 0;
  g_published = 0;
  g_sessionHadSet = false;
  if (!lateCount) g_repeatPending = false;  // a count decided only now still gets its second burst
  checkDrift(now);
}

void housekeeping(uint32_t now) {
  if (digitalRead(BEACON_INT_PIN) == HIGH) g_serviceNow = true;  // safety net for a lost edge
  if (!g_cal.valid) {
    blinkError();
    if (elapsed(now, g_lastCalAttemptMs, BEACON_CAL_RETRY_MS)) calibrate(now);
    return;
  }
  if (!g_counter.setActive() && now - g_lastDriftCheckMs >= BEACON_DRIFT_CHECK_MS) checkDrift(now);
  if (!g_counter.setActive()) g_weight.housekeeping(now);
}

// ---------------------------------------------------------------- console

const char* phaseName(gym::RepPhase p) {
  switch (p) {
    case gym::RepPhase::Rearm: return "rearm";
    case gym::RepPhase::Holdoff: return "holdoff";
    case gym::RepPhase::Count:
    default: return "count";
  }
}

void printSpec(const char* label, const PhaseSpec& p) {
  if (!p.valid) {
    Console.printf("  %s: (not armed)\n", label);
    return;
  }
  Console.printf("  %s: |a| %c %u mg for %u samples (%u ms)\n", label, p.high ? '>' : '<',
                p.thresholdMg, p.duration, p.duration * 20u);
}

// The rep check's settings and what it decided in the last set.
void printCheck() {
  if (g_settings.mode != MotionMode::Stack) {
    Console.println(F("rep check: off in this mode (every dip counts)"));
    return;
  }
  Console.printf("rep check: %s, travel >= %u mm and >= %u %% of the set's reps, wake at rest + %u mg\n",
                 g_settings.repCheck ? "on" : "OFF (counts every dip, logs what it would ignore)",
                 g_settings.repMinTravelMm, g_settings.repMatchPct, BEACON_WAKE_DELTA_MG);
  const size_t n = g_validator.moveCount();
  if (n == 0) return;
  Console.printf("  %s: %u counted, %u ignored", g_validator.active() ? "this set so far" : "last set",
                 g_validator.published(), g_validator.ignored());
  const uint32_t t0 = g_validator.move(0).dipMs;
  uint8_t shown = 0;
  for (size_t i = 0; i < n && shown < 8; i++) {
    const gym::MoveInfo m = g_validator.move(i);
    if (gym::moveCounted(m.verdict) || m.verdict == gym::MoveVerdict::Pending || m.verdict == gym::MoveVerdict::Held) continue;
    Console.printf("%s %.1f s %s", shown == 0 ? ":" : ",", (m.dipMs - t0) / 1000.0f, gym::moveVerdictName(m.verdict));
    shown++;
  }
  Console.println();
}

void printStatus() {
  Console.printf("%s #%u, tx %+d dBm\n", gym::equipmentName(BEACON_EQUIPMENT_TYPE),
                BEACON_MACHINE_INSTANCE, BEACON_TX_POWER_DBM);
  Console.printf("mode %s, axis %s", modeName(g_settings.mode), axisName(g_settings.axis));
  if (g_cal.valid) {
    Console.printf(" -> %s, rest %u mg (x %d, y %d, z %d)\n", axisName(g_cal.axis), g_cal.restMg,
                  g_cal.meanMg[0], g_cal.meanMg[1], g_cal.meanMg[2]);
  } else {
    Console.println(F(", NOT CALIBRATED"));
  }
  printSpec("count", g_countSpec);
  printSpec("rearm", g_rearmSpec);
  Console.printf("  hold-off %lu ms, min rep interval %lu ms, set timeout %u ms\n",
                static_cast<unsigned long>(g_settings.holdoffMs),
                static_cast<unsigned long>(g_settings.minRepIntervalMs), BEACON_SET_TIMEOUT_MS);
  Console.printf("phase %s, reps %u, set %s (seq %u), INT1 pin %d\n", phaseName(g_counter.phase()),
                g_counter.reps(), g_counter.setActive() ? "active" : (g_counter.woken() ? "woken" : "idle"),
                g_counter.setSeq(), digitalRead(BEACON_INT_PIN));
  printCheck();
  Console.print(F("battery "));
  printBattery(readBatteryMv(), usbPowered());
  Console.printf(", reported every %lu s\n", static_cast<unsigned long>(BEACON_STATUS_INTERVAL_MS / 1000));
  ble_console::printStatus(Console, millis());
  g_weight.printSummary();
}

void printConfig() {
  static const char* const kAxisDefines[] = {"kAxisX", "kAxisY", "kAxisZ", "kAxisAuto"};
  Console.printf("#define BEACON_MOTION_MODE %s\n", modeConfigName(g_settings.mode));
  Console.printf("#define BEACON_AXIS %s\n", kAxisDefines[g_settings.axis <= kAxisAuto ? g_settings.axis : kAxisAuto]);
  Console.printf("#define BEACON_COUNT_DELTA_MG %u\n", g_settings.countDeltaMg);
  Console.printf("#define BEACON_REARM_DELTA_MG %u\n", g_settings.rearmDeltaMg);
  Console.printf("#define BEACON_COUNT_DURATION %u\n", g_settings.countDuration);
  Console.printf("#define BEACON_REARM_DURATION %u\n", g_settings.rearmDuration);
  Console.printf("#define BEACON_HOLDOFF_MS %lu\n", static_cast<unsigned long>(g_settings.holdoffMs));
  Console.printf("#define BEACON_MIN_REP_INTERVAL_MS %lu\n",
                static_cast<unsigned long>(g_settings.minRepIntervalMs));
  Console.printf("#define BEACON_REP_CHECK %d\n", g_settings.repCheck ? 1 : 0);
  Console.printf("#define BEACON_REP_MIN_TRAVEL_MM %u\n", g_settings.repMinTravelMm);
  Console.printf("#define BEACON_REP_MATCH_PCT %u\n", g_settings.repMatchPct);
}

void onSettingsChanged(bool needsRecalibration) {
  g_counter.setConfig(counterConfig(g_settings));
  g_validator.setConfig(validatorConfig(g_settings));  // chk on/off itself applies from the next set
  if (needsRecalibration) {
    calibrate(millis());
  } else {
    if (g_cal.measured) {  // keep the last resting reading, re-derive the thresholds
      g_cal.valid = checkRest(g_settings, g_cal.restMg) == nullptr;
      deriveSpecs();
    }
    syncArm(true);
  }
  printStatus();
}

void onRecalibrate() {
  calibrate(millis());
  printStatus();
}

void onResetCount() {
  g_counter.endSet();
  syncRecorder(millis());
  g_repeatPending = false;
  g_setReps = 0;
  syncArm(true);
  Console.println(F("set ended"));
}

// Boot messages are lost when nobody listens (LOG only prints to a connected monitor or phone), so
// the banner and status are repeated to each monitor and phone that connects.
void printBanner() {
  Console.printf("GetMeFit beacon ready: %s #%u. Type h for commands.\n",
                gym::equipmentName(BEACON_EQUIPMENT_TYPE), BEACON_MACHINE_INSTANCE);
  printStatus();
}

bool g_phoneCommand = false;  // the command being run came from the Bluetooth console

bool onExtraCommand(const char* cmd, const char* arg) { return g_weight.command(cmd, arg, g_phoneCommand); }

void onExtraHelp() { g_weight.printHelp(); }

const TuningConsole::Hooks kConsoleHooks = {printStatus,  printConfig,    onSettingsChanged, onRecalibrate,
                                            onResetCount, onExtraCommand, onExtraHelp};
TuningConsole g_console(g_settings, kConsoleHooks);
TuningConsole::Line g_usbLine;
TuningConsole::Line g_phoneLine;

// Teleplot (VS Code extension) plots lines of the form ">name:value". Prints the sample the 50 Hz
// sampler just took (the same one the weight recorder stores), never reading the sensors itself.
void streamSample() {
  if (!Serial || Serial.availableForWrite() < 200) return;  // never block on a full USB buffer
  const uint8_t axis = g_cal.valid ? g_cal.axis : kAxisZ;
  Serial.printf(">a:%d\n>cnt:%u\n>rearm:%u\n>int:%d\n>reps:%u\n", abs(g_weight.latestAccMg()[axis]),
                g_countSpec.thresholdMg, g_rearmSpec.thresholdMg, digitalRead(BEACON_INT_PIN),
                g_counter.reps());
  if (g_validator.active()) {  // the rep check: stack position (mm) and the leaky velocity (m/s)
    Serial.printf(">pos:%.0f\n>vel:%.2f\n>counted:%u\n", g_validator.positionMm(), g_validator.velocity(),
                  g_validator.published());
  }
  g_weight.printStreamFields();  // >tx >ty >tz (TMAG5273), >lx >ly >lz (LIS2MDL), in uT
}

}  // namespace

// ---------------------------------------------------------------- Arduino entry points

void setup() {
  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_RED, LOW);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, LOW);
#ifdef PIN_NEOPIXEL_POWER
  pinMode(PIN_NEOPIXEL_POWER, OUTPUT);  // the variant switches NeoPixel power on; we never use it
  digitalWrite(PIN_NEOPIXEL_POWER, LOW);
#endif
  Serial.begin(115200);  // never wait for a host: the beacon normally runs on its battery

  // Interrupt plumbing before the sensor is configured, so the first edge cannot be lost.
  pinMode(BEACON_INT_PIN, INPUT_PULLDOWN);  // a loose jumper reads LOW instead of floating
  g_intSem = xSemaphoreCreateBinary();
  attachInterrupt(BEACON_INT_PIN, onInt1Rise, RISING);

  Wire.begin();
  Wire.setClock(400000);
  while (!g_imu.begin(Wire, BEACON_LIS3DH_ADDR)) {
    LOG("[err] no LIS3DH at 0x%02X - check SDA/SCL/3V/GND\n", BEACON_LIS3DH_ADDR);
    blinkError();
    delay(2000);
  }

  ble_console::configure();  // before Bluefruit starts (inside g_adv.begin)
  const bool bleOk = g_adv.begin(BEACON_TX_POWER_DBM, BEACON_ADV_INTERVAL_MS);
  if (!bleOk) LOG("[err] Bluefruit did not start\n");
#if BEACON_BLE_CONSOLE
  if (bleOk && !ble_console::begin(millis())) LOG("[err] Bluetooth console did not start\n");
#endif
  g_counter.seedSetSeq(randomByte());  // random, so a reboot does not repeat the last set number
  // After Bluefruit: the taught weights live in its flash file system, and flash writes only
  // complete while the SoftDevice runs. The boot id goes with every stored set (set_store.h).
  const uint16_t bootId = static_cast<uint16_t>((randomByte() << 8) | randomByte());
  g_weight.begin(Wire, g_imu, bleOk, millis(), bootId);

  delay(500);  // let hands get off the board after a reset
  calibrate(millis());
  g_lastHousekeepingMs = millis();
  g_hostConnected = Serial;  // a monitor that is already open gets the banner once, here
  if (g_hostConnected) {
    ConsoleOut::Only usbOnly(Console, ConsoleOut::kUsb);
    printBanner();
  }
}

void loop() {
  uint32_t now = millis();
  const bool usb = usbPowered();

  // Sleep until the earliest deadline (or until INT1 rises).
  uint32_t wait = BEACON_HOUSEKEEPING_MS;
  auto consider = [&](uint32_t deadline) {
    const int32_t left = static_cast<int32_t>(deadline - now);
    const uint32_t ms = left < 0 ? 0 : static_cast<uint32_t>(left);
    if (ms < wait) wait = ms;
  };
  uint32_t deadline = 0;
  if (g_counter.nextDeadline(&deadline)) consider(deadline);
  if (g_repeatPending) consider(g_repeatAtMs);
  if (g_advActive) consider(g_advStopAtMs);
  // The weight recorder samples on battery too: its deadline is not gated on USB.
  if (g_weight.sampling()) consider(g_weight.nextSampleMs());
  consider(g_lastHousekeepingMs + BEACON_HOUSEKEEPING_MS);
  // A console is open (USB, or a phone on Bluetooth): look for typed commands every 50 ms.
  if ((usb || ble_console::connected()) && wait > kConsoleTickMs) wait = kConsoleTickMs;
  if (usb && g_weight.dumping() && wait > kDumpTickMs) wait = kDumpTickMs;
  if (g_serviceNow) wait = 0;

  const bool edge = xSemaphoreTake(g_intSem, ticksCeil(wait)) == pdTRUE;
  now = millis();

  if (edge || g_serviceNow) {
    g_serviceNow = false;
    serviceInterrupt(now);
  }

  if (g_counter.poll(now)) onSetEnded(now);
  syncRecorder(now);
  syncArm();

  if (g_advActive && due(now, g_advStopAtMs)) endBurst();
  if (g_repeatPending && due(now, g_repeatAtMs)) {
    g_repeatPending = false;
    advertise(g_lastFrame, now);
  }
  maybeSendStatus(now);  // loop() runs at least every 2 s (housekeeping), which is soon enough
  const bool phoneJustListening = ble_console::poll(now);  // before the advertising decision
  syncConsoleAdvertising(now);

  if (g_weight.tick(now)) {
    if (g_validator.active()) {
      g_validator.addSample(now, g_weight.latestAccMg());
      publishChecked(now);
    }
    if (usb && g_console.streaming()) streamSample();
  }

  if (elapsed(now, g_lastHousekeepingMs, BEACON_HOUSEKEEPING_MS)) {
    g_lastHousekeepingMs = now;
    housekeeping(now);
  }

  if (usb) {
    const bool host = Serial;
    if (host && !g_hostConnected) {
      ConsoleOut::Only usbOnly(Console, ConsoleOut::kUsb);
      printBanner();
    }
    g_hostConnected = host;
    g_console.poll(Serial, g_usbLine);
    g_weight.pumpDump();
  } else {
    g_hostConnected = false;
  }
  if (phoneJustListening) {
    ConsoleOut::Only phoneOnly(Console, ConsoleOut::kPhone);
    printBanner();
  }
  if (ble_console::ready()) {
    const bool messageEnded = ble_console::takeMessageEnded();  // before reading: see ble_console.h
    g_phoneCommand = true;
    g_console.poll(ble_console::input(), g_phoneLine);
    if (messageEnded) g_console.endLine(g_phoneLine);
    g_phoneCommand = false;
  }
  g_weight.setStreaming(usb && g_console.streaming(), now);
}
