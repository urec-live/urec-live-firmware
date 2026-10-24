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
#include <Arduino.h>
#include <Wire.h>
#include <bluefruit.h>
#include <stdlib.h>
#include <string.h>

#include "beacon_config.h"
#include "gym_protocol.h"
#include "ibeacon_adv.h"
#include "lis3dh_motion.h"
#include "motion_settings.h"
#include "rep_counter.h"
#include "tuning_console.h"

#define LOG(...)                                \
  do {                                          \
    if (Serial) Serial.printf(__VA_ARGS__);     \
  } while (0)

namespace {

const uint32_t kConsoleTickMs = 50;
const uint32_t kStreamTickMs = 20;  // one LIS3DH sample at 50 Hz
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
MotionSettings g_settings = settingsFromConfig();
gym::RepCounter g_counter(counterConfig(g_settings));
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
uint16_t g_setReps = 0;  // reps of the set in progress (for the end-of-set log)

uint32_t g_lastHousekeepingMs = 0;
uint32_t g_lastCalAttemptMs = 0;
uint32_t g_lastDriftCheckMs = 0;
uint32_t g_lastStreamMs = 0;
bool g_hostConnected = false;  // a serial monitor has the USB port open

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
void syncArm(bool force = false) {
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

void advertise(const gym::BeaconFrame& frame) {
  led(true);
  if (!g_adv.burst(frame, BEACON_ADV_EVENTS_PER_BURST)) LOG("[err] advertising failed\n");
  led(false);
}

void onRepCounted(uint32_t now) {
  g_setReps = g_counter.reps();
  g_lastFrame.equipmentType = BEACON_EQUIPMENT_TYPE;
  g_lastFrame.machineInstance = BEACON_MACHINE_INSTANCE;
  g_lastFrame.setSeq = g_counter.setSeq();
  g_lastFrame.reps = g_counter.reps();
  g_lastFrame.txPower = BEACON_RSSI_AT_1M;
  LOG("[rep] %u  (set seq %u)\n", g_lastFrame.reps, g_lastFrame.setSeq);
  advertise(g_lastFrame);
  // Paper: "double advertisements to decrease the loss rate"; a newer rep replaces this one.
  g_repeatPending = true;
  g_repeatAtMs = now + BEACON_ADV_REPEAT_DELAY_MS;
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
  g_repeatPending = false;
  g_imu.disarm();
  g_hwArmed = false;
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
  const int drift = abs(mean[g_cal.axis] - g_cal.meanMg[g_cal.axis]);
  if (drift > BEACON_DRIFT_MG) {
    LOG("[cal] resting reading moved %d mg - recalibrating\n", drift);
    calibrate(now);
  }
}

// ---------------------------------------------------------------- events

void serviceInterrupt(uint32_t now) {
  const uint8_t src = g_imu.readInt1Source();  // also clears the latched pin
  if ((src & Lis3dhMotion::kSrcIA) == 0 || !g_hwArmed) return;

  if (g_hwPhase == gym::RepPhase::Count && g_countSpec.valid &&
      (src & Lis3dhMotion::eventBit(g_cal.axis, g_countSpec.high))) {
    if (g_counter.onCountEvent(now)) {
      onRepCounted(now);
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
  LOG("[set] done: %u reps\n", g_setReps);
  g_setReps = 0;
  g_repeatPending = false;
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
    Serial.printf("  %s: (not armed)\n", label);
    return;
  }
  Serial.printf("  %s: |a| %c %u mg for %u samples (%u ms)\n", label, p.high ? '>' : '<',
                p.thresholdMg, p.duration, p.duration * 20u);
}

void printStatus() {
  Serial.printf("%s #%u, tx %+d dBm\n", gym::equipmentName(BEACON_EQUIPMENT_TYPE),
                BEACON_MACHINE_INSTANCE, BEACON_TX_POWER_DBM);
  Serial.printf("mode %s, axis %s", modeName(g_settings.mode), axisName(g_settings.axis));
  if (g_cal.valid) {
    Serial.printf(" -> %s, rest %u mg (x %d, y %d, z %d)\n", axisName(g_cal.axis), g_cal.restMg,
                  g_cal.meanMg[0], g_cal.meanMg[1], g_cal.meanMg[2]);
  } else {
    Serial.println(F(", NOT CALIBRATED"));
  }
  printSpec("count", g_countSpec);
  printSpec("rearm", g_rearmSpec);
  Serial.printf("  hold-off %lu ms, min rep interval %lu ms, set timeout %u ms\n",
                static_cast<unsigned long>(g_settings.holdoffMs),
                static_cast<unsigned long>(g_settings.minRepIntervalMs), BEACON_SET_TIMEOUT_MS);
  Serial.printf("phase %s, reps %u, set %s (seq %u), INT1 pin %d\n", phaseName(g_counter.phase()),
                g_counter.reps(), g_counter.setActive() ? "active" : "idle", g_counter.setSeq(),
                digitalRead(BEACON_INT_PIN));
}

void printConfig() {
  static const char* const kAxisDefines[] = {"kAxisX", "kAxisY", "kAxisZ", "kAxisAuto"};
  Serial.printf("#define BEACON_MOTION_MODE %s\n", modeConfigName(g_settings.mode));
  Serial.printf("#define BEACON_AXIS %s\n", kAxisDefines[g_settings.axis <= kAxisAuto ? g_settings.axis : kAxisAuto]);
  Serial.printf("#define BEACON_COUNT_DELTA_MG %u\n", g_settings.countDeltaMg);
  Serial.printf("#define BEACON_REARM_DELTA_MG %u\n", g_settings.rearmDeltaMg);
  Serial.printf("#define BEACON_COUNT_DURATION %u\n", g_settings.countDuration);
  Serial.printf("#define BEACON_REARM_DURATION %u\n", g_settings.rearmDuration);
  Serial.printf("#define BEACON_HOLDOFF_MS %lu\n", static_cast<unsigned long>(g_settings.holdoffMs));
  Serial.printf("#define BEACON_MIN_REP_INTERVAL_MS %lu\n",
                static_cast<unsigned long>(g_settings.minRepIntervalMs));
}

void onSettingsChanged(bool needsRecalibration) {
  g_counter.setConfig(counterConfig(g_settings));
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
  g_repeatPending = false;
  g_setReps = 0;
  syncArm(true);
  Serial.println(F("set ended"));
}

// Boot messages are lost when no monitor is open (LOG only prints to a connected host), so the
// banner and status are repeated whenever a monitor connects.
void printBanner() {
  Serial.printf("GetMeFit beacon ready: %s #%u. Type h for commands.\n",
                gym::equipmentName(BEACON_EQUIPMENT_TYPE), BEACON_MACHINE_INSTANCE);
  printStatus();
}

const TuningConsole::Hooks kConsoleHooks = {printStatus, printConfig, onSettingsChanged, onRecalibrate,
                                            onResetCount};
TuningConsole g_console(g_settings, kConsoleHooks);

// Teleplot (VS Code extension) plots lines of the form ">name:value".
void streamSample() {
  if (!Serial || Serial.availableForWrite() < 96) return;  // never block on a full USB buffer
  Lis3dhMotion::Sample s;
  bool fresh = false;
  if (!g_imu.readSample(&s, &fresh)) return;
  const uint8_t axis = g_cal.valid ? g_cal.axis : kAxisZ;
  Serial.printf(">a:%d\n>cnt:%u\n>rearm:%u\n>int:%d\n>reps:%u\n", abs(s.mg[axis]),
                g_countSpec.thresholdMg, g_rearmSpec.thresholdMg, digitalRead(BEACON_INT_PIN),
                g_counter.reps());
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

  if (!g_adv.begin(BEACON_TX_POWER_DBM, BEACON_ADV_INTERVAL_MS)) LOG("[err] Bluefruit did not start\n");
  g_counter.seedSetSeq(randomByte());  // random, so a reboot does not repeat the last set number

  delay(500);  // let hands get off the board after a reset
  calibrate(millis());
  g_lastHousekeepingMs = millis();
  g_hostConnected = Serial;  // a monitor that is already open gets the banner once, here
  if (g_hostConnected) printBanner();
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
  consider(g_lastHousekeepingMs + BEACON_HOUSEKEEPING_MS);
  if (usb && wait > kConsoleTickMs) wait = kConsoleTickMs;
  if (usb && g_console.streaming()) consider(g_lastStreamMs + kStreamTickMs);
  if (g_serviceNow) wait = 0;

  const bool edge = xSemaphoreTake(g_intSem, ticksCeil(wait)) == pdTRUE;
  now = millis();

  if (edge || g_serviceNow) {
    g_serviceNow = false;
    serviceInterrupt(now);
  }

  if (g_counter.poll(now)) onSetEnded(now);
  syncArm();

  if (g_repeatPending && due(now, g_repeatAtMs)) {
    g_repeatPending = false;
    advertise(g_lastFrame);
  }

  if (elapsed(now, g_lastHousekeepingMs, BEACON_HOUSEKEEPING_MS)) {
    g_lastHousekeepingMs = now;
    housekeeping(now);
  }

  if (usb) {
    const bool host = Serial;
    if (host && !g_hostConnected) printBanner();
    g_hostConnected = host;
    g_console.poll();
    if (g_console.streaming() && elapsed(now, g_lastStreamMs, kStreamTickMs)) {
      // Keep a steady 50 Hz: step the schedule by one sample, or resync after a long gap.
      g_lastStreamMs = (now - g_lastStreamMs < 2 * kStreamTickMs) ? g_lastStreamMs + kStreamTickMs : now;
      streamSample();
    }
  } else {
    g_hostConnected = false;
  }
}
