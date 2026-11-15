// Per-beacon settings. Everything a user is expected to change lives here.
// The USB tuning console (see docs/rep-counting-walkthrough.md) can change the rep-detection
// values live; its `cfg` command prints them in this format to paste back in.
#pragma once

#include "gym_protocol.h"
#include "motion_settings.h"
#include "weight_settings.h"

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

// ---- Rep check (lib/gym_core/src/rep_validator.h, walkthrough §3.7) ---------------------------
// Stack mode only: a dip counts only if the stack went up and came back down by about as much as
// the set's other reps. That drops the setup (lift and hold) and the release (letting go, standing
// up). The first count shows at rep 2 (0 -> 2); from rep 3 on, each rep counts at its dip as before.
// The console's `chk off` keeps counting every dip and only logs what the check would ignore.
#define BEACON_REP_CHECK 1
#define BEACON_REP_MIN_TRAVEL_MM 80     // a rep moves the stack at least this far up and back down
#define BEACON_REP_MATCH_PCT 50         // ... and at least this % of the median travel of the set's reps
#define BEACON_REP_START_MATCH_PCT 60   // the first two reps' returns agree this well
#define BEACON_REP_TURN_MM 40           // smaller wiggles are not strokes
#define BEACON_REP_TILT_DEG 35          // tilted further from rest: the beacon is being handled
#define BEACON_WAKE_DELTA_MG 48         // a lift starting (|a| > rest + this for 2 samples) starts
                                        // the sampler, so the check sees the stack leave rest

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

// ---- Weight sensing (docs/weight-sensing.md) ---------------------------------------------------
// W8-Scope: a magnetometer on the stack's top plate sees how many steel slabs were lifted. It plugs
// into the LIS3DH's spare STEMMA QT socket (schematic §8). Without one, nothing below runs.
#define BEACON_MAG_SENSOR MagSensor::Auto  // Auto | Tmag5273 | Lis2mdl | Both
#define BEACON_TMAG5273_ADDR 0x35
#define BEACON_LIS2MDL_ADDR 0x1E
#define BEACON_TMAG_OVERSAMPLE 4   // 32x conversions averaged per 50 Hz sample (1..5; 4 = 13.4 ms)
#define BEACON_W8_MAX_SET_S 60     // recording buffer; a longer set keeps its last 60 s
#define BEACON_W8_MIN_REP_MS 2000  // paper's dT between rep tops (shortened for faster counted reps)
// Taught weights are plain numbers with one decimal (`teach 12.5`). Use the machine's unit: lb
// matches the backend's weightLbs.

// ---- Advertising ------------------------------------------------------------------------------
#define BEACON_TX_POWER_DBM 4        // nRF52840: -40,-20,-16,-12,-8,-4,0,2..8
#define BEACON_RSSI_AT_1M (-55)      // iBeacon "measured power"; informational
#define BEACON_ADV_INTERVAL_MS 100
#define BEACON_ADV_EVENTS_PER_BURST 2
#define BEACON_ADV_REPEAT_DELAY_MS 1400  // second burst per rep (paper: "double advertisements")
// Status frame (battery voltage) at boot and then this often, held back while a set is active.
// One burst costs ~0.2 uA averaged at 5 min. The gateway hears it without any change to its scan.
#define BEACON_STATUS_INTERVAL_MS 300000

// ---- Bluetooth console (ble_console.h) ---------------------------------------------------------
// The tuning console on a phone, over the Nordic UART Service: Adafruit's Bluefruit Connect app
// (UART), nRF Connect or Serial Bluetooth Terminal. The beacon shows up as "GetMeFit Leg Curl #1".
// Anyone in range can connect while it advertises, so it only does so for a window.
#define BEACON_BLE_CONSOLE 1                    // 0 = USB console only
#define BEACON_BLE_CONSOLE_WINDOW_S 600         // connectable this long after power-up and after a
                                                // phone disconnects; 0 = whenever no phone is connected
#define BEACON_BLE_CONSOLE_ADV_INTERVAL_MS 500  // connectable advertising: some tens of uA while open

// ---- Misc -------------------------------------------------------------------------------------
#define BEACON_LED_FEEDBACK 1        // red LED on during each burst; set 0 to save power
#define BEACON_HOUSEKEEPING_MS 2000  // safety-net wake-up interval
