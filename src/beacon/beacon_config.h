// Per-beacon settings. Everything a user is expected to change lives here.
// The USB tuning console (see docs/rep-counting-walkthrough.md) can change the rep-detection
// values live; its `cfg` command prints them in this format to paste back in.
#pragma once

#include "gym_protocol.h"
#include "motion_settings.h"

// ---- Identity ---------------------------------------------------------------------------------
// Paper: "the UUID chunk is pre-programmed to represent the gym equipment type".
// The gateway maps (type, instance) to a machine code in src/gateway/gateway_config.h.
#define BEACON_EQUIPMENT_TYPE gym::kEquipLegCurl
#define BEACON_MACHINE_INSTANCE 1  // 1..255: Leg Curl #1 -> LC001

// ---- Wiring (beacon-breadboard-schematic.md §8) -----------------------------------------------
#define BEACON_INT_PIN 5        // Feather D5 (H14) <- LIS3DH INT1 (F23), jumper H23 -> I14
#define BEACON_LIS3DH_ADDR 0x18

// ---- Rep detection ----------------------------------------------------------------------------
// Stack (default): COUNT 64 / REARM 64 / dur 4 / 2 / hold-off 350
// Paper:           COUNT 64 / REARM 48 / dur 4 / 2 / hold-off 0      (REARM = recovery hysteresis)
// Lever:           COUNT 350 / REARM 150 / dur 4 / 4 / hold-off 200  (set BEACON_AXIS explicitly)
// 64 mg = 4 sensor steps: catches 30 cm lifts of up to ~1.5 s; very slow tempos need 48.
#define BEACON_MOTION_MODE MotionMode::Stack
#define BEACON_AXIS kAxisAuto
#define BEACON_COUNT_DELTA_MG 64
#define BEACON_REARM_DELTA_MG 64
#define BEACON_COUNT_DURATION 4  // samples, 20 ms each at 50 Hz
#define BEACON_REARM_DURATION 2
#define BEACON_HOLDOFF_MS 350
#define BEACON_MIN_REP_INTERVAL_MS 600

// Paper: "accumulated until no new interrupt signal is received in five seconds and reset to zero".
#define BEACON_SET_TIMEOUT_MS 5000
#define BEACON_MAX_REPS_PER_SET 99

// ---- Calibration / orientation guard ----------------------------------------------------------
#define BEACON_MIN_GRAVITY_MG 700   // Stack/Paper: the motion axis must carry at least this much g
#define BEACON_STILL_P2P_MG 48      // calibration only accepts samples this still (peak-to-peak)
#define BEACON_DRIFT_MG 32          // resting reading moved this much -> recalibrate (keep it below
                                    // half the smallest delta, or a drift eats the count margin)
#define BEACON_DRIFT_CHECK_MS 30000 // how often to check for drift while idle
#define BEACON_CAL_RETRY_MS 10000   // retry interval while calibration keeps failing

// ---- Advertising ------------------------------------------------------------------------------
#define BEACON_TX_POWER_DBM 4        // nRF52840: -40,-20,-16,-12,-8,-4,0,2..8
#define BEACON_RSSI_AT_1M (-55)      // iBeacon "measured power"; informational
#define BEACON_ADV_INTERVAL_MS 100
#define BEACON_ADV_EVENTS_PER_BURST 2
#define BEACON_ADV_REPEAT_DELAY_MS 1400  // second burst per rep (paper: "double advertisements")

// ---- Misc -------------------------------------------------------------------------------------
#define BEACON_LED_FEEDBACK 1        // red LED on during each burst; set 0 to save power
#define BEACON_HOUSEKEEPING_MS 2000  // safety-net wake-up interval
