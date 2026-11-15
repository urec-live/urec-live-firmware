# Rep Counting: How It Works

This guide explains the rep-counting system in this project, from the physics of a weight stack to the bytes on the radio and the JSON in the database. It reproduces the setup of:

> S. Bian, A. Rupp, M. Magno, *Fully Automatic Gym Exercises Recording: An IoT Solution*, IEEE MetroInd4.0&IoT 2023 ([arXiv 2305.17594](https://arxiv.org/abs/2305.17594))

It uses the breadboard beacon from `../beacon-breadboard-schematic.md` and an ESP32-C3-DevKitM-1 as the gateway. Weight sensing (a magnetometer on the same beacon) has its own guide: [weight-sensing.md](weight-sensing.md).

---

## 0. Quick start

1. **Add wire 9:** a flexible jumper from **H23 to I14** (LIS3DH INT1 → Feather D5). See schematic §8.
2. **Flash the beacon** (Feather plugged in by USB):
   `pio run -e beacon -t upload`, then `pio device monitor -e beacon`.
   When the monitor connects, the beacon prints `GetMeFit beacon ready` and its status, including `axis … -> z, rest … mg`. Boot lines such as `[cal] …` only show up if the monitor was already open; type `s` at any time to see the status again.
3. **Count a rep:** lift the breadboard about 30 cm, pause, and put it back down. The red LED flashes and the monitor prints `[rep] 1`.
4. **Flash the gateway** (ESP32-C3 plugged in): `pio run -e gateway -t upload`, then `pio device monitor -e gateway`. Every rep now prints `[reps] LC001=…`.
5. **Fill in `src/gateway/secrets.h`:** copy `secrets.example.h` and fill it in (§9, §10) to turn on Wi-Fi, Firebase and UREC Live.

Every step is explained below.

---

## 1. The paper and this implementation

The paper's system has four parts:
- A **beacon** on each machine: an nRF52840 plus an LIS3DH accelerometer.
- A **gateway**: an ESP32-C3 that collects beacon data and uploads it.
- A **smartwatch**: it picks the beacon with the strongest signal, i.e. the machine the wearer is on.
- A **Firebase dashboard**.

The beacon does almost nothing. The accelerometer watches the machine. When the acceleration crosses a threshold, the accelerometer raises an **interrupt**, a wire going HIGH. The microcontroller wakes up, adds one to the rep count, broadcasts the count by Bluetooth, and goes back to sleep.

The paper's key sentences, and where each one lives in this code:

| Paper | This project |
|---|---|
| "the inertial sensor can be configured to generate interrupt signals … only when a threshold is reached in a particular direction" | LIS3DH inertial interrupt 1 on INT1 → Feather D5, HP filter off so gravity defines the direction (`src/beacon/lis3dh_motion.cpp`) |
| "The minor data is updated for broadcasting the repetition number, which corresponds to the number of interrupts" | one counted interrupt = one rep, sent as the iBeacon *minor* (`lib/gym_core/src/rep_counter.*`, `gym_protocol.*`) |
| "UUID chunk is pre-programmed to represent the gym equipment type" | UUID = 14-byte GetMeFit base + 2-byte equipment type (`gym_protocol.h`) |
| "accumulated until no new interrupt signal is received in five seconds and reset to zero" | `BEACON_SET_TIMEOUT_MS 5000` |
| "we set double advertisements to decrease the loss rate" | two advertising bursts per rep, 1.4 s apart (`BEACON_ADV_REPEAT_DELAY_MS`) |
| "we stored each beacon identifier in the gateway beforehand" | `kMachines[]` in `src/gateway/gateway_config.h` |
| "a permanently loaded repetition vector … the gateway sends an updated HTTP PATCH packet to the Google database … JSON-form body" | `MachineTracker` + one multi-path `PATCH /gym/machines.json` (`src/gateway/main.cpp`) |

### Where this deliberately differs, and why

| Topic | Paper | Here | Why |
|---|---|---|---|
| Beacon hardware | bare nRF52840 module, CR2032 coin cell | Feather nRF52840 Express, 400 mAh LiPo | the hardware you have (see §12 for what it means for battery life) |
| Trigger | one threshold interrupt per rep | **two-phase**: a *count* interrupt, then a *re-arm* interrupt for the opposite movement (§3). Mode `paper` reproduces the single trigger | a single trigger counts twice when the lifter pauses at the top, or when the stack bounces at the bottom |
| *major* field | not specified | high byte = set sequence number, low byte = machine instance | the gateway can tell sets apart, and two machines of the same type apart |
| 5 s reset | timed from the last interrupt | timed from the last motion event (count *or* re-arm) | a slow 6 s rep is not split into separate 1-rep sets |
| Gateway radio | 3 s scan, then 0.9 s upload, alternating | scans **continuously**; uploads run alongside | the paper's gateway "missed the last package quite often" because it was uploading at the time |
| Dashboard / watch | Freeboard dashboard, LilyGo wristband | Firebase console (or your app), plus the UREC Live machine status | per your choices; the watch comes later |

The paper published no code or register values. Everything below was derived from the LIS3DH datasheet (DocID17530), ST application note AN3308, and the library sources.

---

## 2. System overview

```
  ┌──────────── on the weight stack ────────────┐
  │  LIS3DH ──INT1 (wire 9)──► Feather nRF52840 │      Bluetooth LE, iBeacon
  │   │  I2C (SDA/SCL)          sleeps, counts, ├─────► advertisements ──────┐
  │   └─────────────────────────advertises      │     (minor = reps)         │
  └─────────────────────────────────────────────┘                            ▼
                                                            ┌──────────────────────────┐
                                                            │ ESP32-C3 gateway         │
                                                            │ scan → queue → tracker   │
                                                            └───────┬───────────┬──────┘
                                                        Wi-Fi HTTPS │           │ HTTP(S)
                                                                    ▼           ▼
                                                  Firebase Realtime DB     UREC Live backend
                                                  /gym/machines/LC001      PUT /api/machines/code/
                                                  /gym/sets/LC001/…            LC001/status
```

| Folder | What it is |
|---|---|
| `lib/gym_core/` | Board-independent C++: the radio packet format, the rep counter, the gateway's machine tracker. Compiled into both firmwares and unit-tested. |
| `src/beacon/` | Feather firmware (`pio run -e beacon`) |
| `src/gateway/` | ESP32-C3 firmware (`pio run -e gateway`) |
| `test/test_gym_core/` | Unity tests for `lib/gym_core` (§11) |
| `docs/` | This file and the Firebase security rules |

`platformio.ini` holds both firmwares. `build_src_filter` makes each environment compile only its own folder.

---

## 3. The physics of a rep, and how the beacon counts it

### 3.1 What the accelerometer feels

An accelerometer at rest does not read zero. It reads gravity: +1 g (≈1000 mg) on whichever axis points up. Gravity is about 1000 mg; the LIS3DH reports milli-g.

When the stack accelerates **upward**, the sensor reads **more** than 1 g. When it accelerates **downward**, it reads **less**. It's the same feeling as an elevator starting up (heavier) or starting down (lighter). Moving at constant speed reads exactly 1 g again.

The LIS3DH's interrupt comparator works on the **absolute value** `|a|` (AN3308 §6.3: "the unsigned acceleration value"). So there is no "positive" or "negative" setting. Direction comes from gravity:
- With the axis vertical, `|a|` sits near 1000 mg.
- Downward acceleration pulls `|a|` below 1000 mg.
- Upward acceleration pushes it above.

This is the paper's "a particular direction". It is also why the paper failed when the beacon shifted: "a slight displacement caused the beacon to lose the orientation used for interrupt generation". Tilt shrinks the gravity on the axis, so the thresholds no longer sit where they should.

### 3.2 One rep on a weight stack

The beacon sits on the stack. Its Z axis is vertical, and at rest it reads 1000 mg.

```
 |a| (mg)
 1200 ┤        ╭╮                                    ╭─╮
 1064 ┤┄┄┄┄┄┄┄┄┼┼┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┼┄┼┄┄┄┄  re-arm threshold (rest + 64)
 1000 ┤────────╯╰───────╮           ╭────────────────╯ ╰────  rest (gravity only)
  936 ┤┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┼┄┄┄┄┄┄┄┄┄┄┄┼┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄┄  count threshold (rest − 64)
  850 ┤                 ╰───────────╯
       └─ lift starts   └─ slows near the top,  └─ lowers at       └─ stops at the bottom
          (pushed up)      starts back down        steady speed       (pushed up again)
                           ► COUNT                                     ► RE-ARM, hold-off
```

Per rep there is exactly one **dip** (slowing at the top plus starting down) and one **rise** (stopping at the bottom plus the next lift). These are small effects. For a smooth 30 cm stroke the peak is (A/2)(π/T)²: about 0.15 g for a 1 s lift, 0.07 g for 1.5 s and 0.04 g for 2 s. That is why the default thresholds are only **64 mg** (4 steps of 16 mg) from rest. By this estimate 64 mg catches lifts of up to about 1.5 s. Real turnarounds are usually sharper than a smooth sine, but for very slow tempos lower both deltas to 48 (`dl 48`, `dh 48`, §7). Below that, the interrupt threshold's 16 mg step gets close.

### 3.3 Why one threshold is not enough

The paper counts one interrupt per rep. With a single threshold, real lifting breaks it:

- **Pause at the top** (very common on leg curl, leg extension and lat pulldown): the slowing before the pause and the start of the descent after it are two separate dips, with 1 g in between. A dip-only counter counts **2** per rep.
- **Stack touches down and bounces:** the landing spike and its ringing cross a rise threshold several times. A rise-only counter also counts the stack landing after the last rep, so it gives N+1 or 2N.

### 3.4 The two-phase counter

The firmware keeps the paper's idea, "each counted hardware interrupt is one rep", but always arms the LIS3DH for exactly one of two conditions:

```
   COUNT phase  ── dip (|a| < rest − 64 for 80 ms) ──►  rep counted, radio burst
        ▲                                                       │
        │                                                       ▼
   (350 ms hold-off) ◄── rise (|a| > rest + 64 for 40 ms) ──  REARM phase
```

- A **pause at the top** produces two dips but no rise between them. The second dip arrives while the sensor is armed for the rise, so it is never seen. Result: one count.
- A **bounce at the bottom** happens just after the rise (the re-arm event). During the 350 ms hold-off the interrupt is switched off, so the ringing is ignored.
- The **first rep** of a set starts from rest. The lift start is a rise, but the counter is armed for a dip, so nothing happens until the top. Result: no N+1.
- The **set timer** (5 s, as in the paper) restarts on *every* event, count or re-arm. A slow controlled rep (2 s up, 1 s squeeze, 3 s down) has an event at the top and one at the bottom, so it never goes 5 s without one. That assumes its dip and rise are big enough to cross the thresholds; a lift that slow may need `dl 48`/`dh 48` (§3.2).

The state machine is `gym::RepCounter` in `lib/gym_core/src/rep_counter.cpp`. It has no hardware code, so the unit tests can feed it these exact scenarios (§11).

### 3.5 The three modes

The thresholds are always relative to the **resting reading** measured at calibration:

| Mode | Count when | Re-arm when | Hold-off | Use for |
|---|---|---|---|---|
| `stack` (default) | \|a\| < rest − 64 mg for 4 samples | \|a\| > rest + 64 mg for 2 samples | 350 ms | beacon on the weight stack |
| `paper` | \|a\| < rest − 64 mg for 4 samples | \|a\| > rest − 16 mg for 2 samples (recovered by 48 mg) | 0 | comparison with the paper's single trigger |
| `lever` | \|a\| > rest + 350 mg (≈20° of tilt) for 4 samples | \|a\| < rest + 200 mg for 4 samples | 200 ms | beacon on a leg-curl/extension lever arm |

**Lever mode** works differently. The axis you choose should be **horizontal** at rest, so it reads ≈0 mg. As the lever swings up by θ, gravity appears on that axis as sin θ × 1000 mg. That gives a count above ~20° of tilt and a re-arm below ~11.5°. Choose the axis explicitly with `BEACON_AXIS` (or the `axis` command). In lever mode `auto` simply falls back to X, which is only right if X is the axis that tilts. Calibration refuses a lever axis that reads more than 500 mg at rest (more than ~30° off horizontal). On such an axis, tilting would lower \|a\| and nothing would ever count.

### 3.6 Calibration and the orientation guard

At boot, after `cal`, and whenever the resting reading drifts, the beacon:

1. **Reads 32 samples** (about 0.7 s). They are rejected if any axis moves more than 48 mg peak-to-peak, which means someone is touching the machine.
2. **Picks the axis.** `auto` takes the axis carrying the most gravity. `stack` and `paper` need at least 700 mg on it, so the axis must be within about 45° of vertical. A board lying on its side would read ~0 mg on that axis and, in a dip mode, "dip" forever, which would be runaway counting.
3. **Derives both thresholds** from the resting reading `rest` on that axis.

If calibration fails, the beacon blinks red twice every 2 s and retries every 10 s.

Every calibration first rewrites the LIS3DH's whole configuration (`Lis3dhMotion::begin()`). A sensor that lost power, for example through a VIN/GND jumper shaken loose for a moment, comes back powered down and silent. The drift check notices that no samples arrive and recalibrates, and the 10 s retry keeps trying until the sensor answers again.

While no set is active, the beacon re-measures **every 30 s and after every set**. If it is still and the reading has moved more than 32 mg (it was bumped, or a zip tie slipped), it recalibrates. The limit is half the default 64 mg stack deltas. A larger tolerated drift would eat the count margin and could trigger phantom reps. If you lower `dl`/`dh` to 48, consider `BEACON_DRIFT_MG 24`. This is the fix for the paper's one failure.

### 3.7 Checking each move is a rep

Any downward acceleration is a dip, so the two-phase counter also counts moves that are not reps. At the first gym test (lat pulldown, 10 Nov) the extra counts came from two places:

- **Setup:** standing to reach the bar, then sitting down with it, lifts the stack and holds it up. The stop at the top of that lift is a dip.
- **Release:** after the last rep, letting go drops the stack (free fall reads ≈0 g, a deep dip), and standing up with the bar lowers it.

So in stack mode the beacon now checks every dip against what the stack actually did. A dip counts only if it belongs to a **rep cycle**: the stack went **up, then came back down**, by an amount like the set's other reps. A setup is a rise with no return. A release is a descent with no rise.

**Following the stack.** A second LIS3DH interrupt (§4) wakes the beacon when a lift starts: \|a\| > rest + 48 mg for 2 samples, armed only while no set is active. That starts a **session**, and with it the 50 Hz sampler, while the stack is still at rest. `gym::RepValidator` (`lib/gym_core/src/rep_validator.*`) then follows the stack's height from the samples:

1. **Acceleration along gravity.** It projects each sample on the low-passed gravity direction and subtracts gravity: a = acc · û − g0. A tilted beacon works, and side-to-side rattle, which inflates \|a\|, does not bias it. The result is low-passed at 4 Hz.
2. **Strokes.** A leaky velocity (τ = 1 s) gives the direction, up or down, and switches when it passes ±0.06 m/s. Each stroke (one direction) is integrated on its own, from v = 0 at its start, and de-drifted so that v = 0 at its end too. That cancels any constant accelerometer bias exactly, with no still moment needed in between: lat pulldown reps run back to back. (Integrating the whole set at once drifted by 2 m in 14 s on the gym data.)
3. **Stops.** The leaky velocity turns a little before the stack does, so a stroke ends while the stack is still moving. If the signal goes quiet within 0.5 s of a stroke's end, the stack has stopped (a hold): its velocity is 0 from there for as long as the quiet lasts. That keeps a held setup from turning into a fake descent. A quiet stretch in the middle of a stroke is left alone: it may as well be a steady speed.
4. **Turning points.** Summing the strokes gives the stack's height; peaks and valleys come from it with 40 mm of hysteresis, so wiggles smaller than that are not strokes.

**Judging a dip.** Each dip the counter counts is a *move*:

| The dip came… | Verdict |
|---|---|
| while the stack was going down (release, standing up) | `norise` |
| while the beacon was tilted more than 35° from where it rests (picked up, flipped) | `tilted` |
| during a lift, which then never came back down (setup) | `noreturn` |
| during a lift that another, later dip in the same rise also belongs to (setup merging into rep 1) | `superseded`: the later one stays |
| during a lift, up and back, but by less than 80 mm or less than 50 % of the median travel of the set's counted reps | `small` |
| during a lift, up and back, big enough | counted |

A move's size is min(rise, return): the rise from the valley before its peak, and the return to the valley after it.

**When counts show.** Before the set has a template, the first cycle is **held** until a second one has come back down too, and their returns agree within 60 %. Then both count at once, and the display goes 0 → 2 as rep 3 starts. If they don't agree, the larger stays held and the other is `dropped`; a lone cycle at the set's end counts as 1, which the gateway already treats as a bump. From rep 3 on, a dip counts **at once** if the rise so far already reaches the template, so the count still moves at the top of each rep. The returns are compared, not the rises, because rep 1 often starts from the setup's hold, higher up than the later reps' bottoms.

**When it cannot judge.** If a session starts at a dip instead of the wake (a lift too gentle to wake it), the first stroke's starting speed is unknown. That first dip then counts **unchecked**, as before, unless a later dip shares its peak. Recordings made before the wake existed (the 10 Nov gym sets) all start that way.

**Modes.** Lever and paper mode are not checked: every dip counts, as before. `chk off` keeps the check running but counts every dip, logging `[move] would ignore: …` for what it would have dropped (§7). The settings are `BEACON_REP_*` and `BEACON_WAKE_DELTA_MG` in `beacon_config.h`; `travel` and `match` change the two thresholds live.

**How it was tuned.** On the 50 sets downloaded after the 10 Nov gym test, nine clean lat pulldown sets have a clear truth from their plots. The check matches it on all nine: setup, release and fidget dips are dropped, and no real cycle is lost. The beacon counted 183 dips in those 50 sets; the check keeps 103. `tools/replay_reps/` replays any downloaded sets through the same C++ (§11). The 15 unit tests (§11) cover the cases the gym data can't, because those sets started mid-motion: anchored setups with holds from 0.5 to 4 s, partial reps at 55 %, pauses, bias, rocking ±17° and a flipped beacon.

---

## 4. The LIS3DH configuration, register by register

`Adafruit_LIS3DH::begin()` is **not** used. It sends "data ready" to INT1 400 times a second, switches on high-resolution mode (not allowed together with low-power mode) and turns on the ADC. Instead, `Lis3dhMotion::configureBase()` writes:

| Register | Value | Bits | Meaning |
|---|---|---|---|
| `INT1_CFG` 0x30, `INT2_CFG` 0x34 | `0x00` | | interrupts off while reconfiguring |
| `CTRL_REG1` 0x20 | `0x47` | ODR=0100, LPen=0, Z/Y/X on | 50 samples/s, normal 10-bit mode (≈11 µA). Weight sensing integrates the acceleration and needs the finer steps; for counting alone, low-power 8-bit mode (`0x4F`, ≈6 µA) would do |
| `CTRL_REG2` 0x21 | `0x00` | HP filter bypassed | **keeps gravity in the signal**: that is the "direction" |
| `CTRL_REG3` 0x22 | `0x60` | I1_IA1, I1_IA2 | interrupt-1 (count/re-arm) and interrupt-2 (the lift-start wake, §3.7) events both drive the INT1 pin |
| `CTRL_REG4` 0x23 | `0x80` | BDU=1, FS=±2 g, HR=0 | 4 mg per data step in normal mode; the interrupt threshold still steps in 16 mg |
| `CTRL_REG5` 0x24 | `0x0A` | LIR_INT1, LIR_INT2 | **latched**: each generator stays set until its `INTx_SRC` is read, and INT1 stays HIGH while either one is |
| `CTRL_REG6` 0x25 | `0x00` | | INT1 active-high, nothing on the INT2 pin |
| `TEMP_CFG` 0x1F | `0x00` | | ADC/temperature off |
| `CLICK_CFG` | `0x00` | | nothing else can raise interrupts |
| read `INT1_SRC` 0x31, `INT2_SRC` 0x35 | | | clears latches left over from before a reset |

Then, for each phase, `Lis3dhMotion::arm()` programs:

| Register | Value | Meaning |
|---|---|---|
| `INT1_CFG` 0x30 | `0x00` first | |
| `INT1_THS` 0x32 | round(threshold mg / 16) | 7 bits, 16 mg per step at ±2 g, max 2032 mg |
| `INT1_DURATION` 0x33 | samples | the condition must hold this many samples (20 ms each at 50 Hz) |
| `INT1_CFG` 0x30 | one enable bit | XLIE `0x01`, XHIE `0x02`, YLIE `0x04`, YHIE `0x08`, ZLIE `0x10`, ZHIE `0x20` ("L" = below threshold, "H" = above). AOI=0 and 6D=0: a plain threshold. |

`Lis3dhMotion::armWake()` programs interrupt 2 the same way (`INT2_CFG` 0x34, `INT2_THS` 0x36, `INT2_DURATION` 0x37) for the wake: \|a\| > rest + 48 mg for 2 samples on the motion axis. It is armed only while no set is active, in stack mode; `disarmWake()` turns it off. When the pin rises, `serviceInterrupt()` reads both `INT1_SRC` and `INT2_SRC`.

**Reading data:** `readSample()` reads 7 bytes in one burst starting at `STATUS_REG` 0x27. The register address is sent as `0xA7`; the top bit means "auto-increment". In normal mode each axis is a 10-bit value, left-justified in its low/high byte pair: `(int16)(L | H << 8) >> 6`, times 4 mg. With BDU=1 the outputs only update once both halves have been read. The burst never touches `REFERENCE` 0x26.

---

## 5. Beacon firmware tour (`src/beacon/`)

| File | Role |
|---|---|
| `beacon_config.h` | all settings: identity, pin, thresholds, timings, radio |
| `motion_settings.*` | modes, presets, and turning "rest + delta" into `PhaseSpec`s |
| `lis3dh_motion.*` | the register-level driver from §4 |
| `ibeacon_adv.*` | Bluefruit advertising bursts: the iBeacon rep frame and the battery status frame (§5.5), and the Bluetooth console's connectable advertisement between bursts (§7) |
| `battery.*` | reads the LiPo voltage through the Feather's VBAT divider (§5.5) |
| `tuning_console.*` | the console commands, on USB and on a phone (§7) |
| `ble_console.*`, `console_out.*` | the Bluetooth console for a phone, and the output that goes to USB and/or the phone (§7) |
| `main.cpp` | boot sequence, sleep loop, interrupt handling, calibration |
| `weight_sensing.*`, `set_recorder.*`, `tmag5273_mag.*`, `magnetometer.h`, `weight_settings.h` | weight sensing with a magnetometer ([weight-sensing.md](weight-sensing.md) §6) |
| `set_store.*`, `qspi_flash.*` | every recorded set kept in the 2 MB QSPI flash, downloaded later with `tools/download_sets.py` ([weight-sensing.md](weight-sensing.md) §8.1) |

### 5.1 Boot order (`setup()`)

1. **LEDs off, NeoPixel power off, `Serial.begin`.** The beacon never waits for a computer, because on battery nobody will connect.
2. **`pinMode(D5, INPUT_PULLDOWN)`.** A shaken-loose jumper then reads LOW instead of floating and firing random reps.
3. **Create the semaphore, then `attachInterrupt(D5, onInt1Rise, RISING)`.** This happens *before* the sensor is configured, so no edge can be lost or arrive before the semaphore exists.
4. **`Wire.begin()` at 400 kHz, then `Lis3dhMotion::begin()`:** checks `WHO_AM_I` = 0x33 and writes §4. Without a sensor the beacon logs an error, blinks and retries every 2 s.
5. **Start the Bluefruit stack (SoftDevice S140)** at +4 dBm, with the blue "connection" LED disabled. Rep and status bursts are non-connectable and non-scannable. The Bluetooth console's UART service starts here too, and between bursts the beacon advertises as connectable for a phone (§7).
6. **Seed the set sequence number** from the SoftDevice's hardware random-number generator. A rebooted beacon then does not reuse the last set's number.
7. **Look for a magnetometer** for weight sensing and load its taught weights. This comes after Bluefruit, because the weights live in its flash file system ([weight-sensing.md](weight-sensing.md) §6.1).
8. **Wait 0.5 s** (hands off), **calibrate** (§3.6), **arm the count phase**.
9. The first pass of `loop()` sends the **battery status frame** (§5.5), so the gateway learns the battery level right after a reset or a battery swap.

### 5.2 Sleeping

`loop()` computes the earliest deadline. The candidates are:
- the hold-off end
- the end of a radio burst, and the repeat burst
- the set timeout
- the 2 s housekeeping tick
- the 50 Hz sampler while a set is recorded for weight sensing (on battery too) or while streaming
- while USB power is present or a phone is connected to the Bluetooth console, also the 50 ms console tick

It then calls `xSemaphoreTake(sem, time until that deadline)`. While the loop task waits, FreeRTOS has nothing to run. The Adafruit core's tickless idle then puts the chip into System ON sleep via `sd_app_evt_wait()`. The CPU wakes for exactly two reasons:
- the **INT1 rising edge**: the tiny ISR `onInt1Rise()` only calls `xSemaphoreGiveFromISR`;
- the **deadline**.

### 5.3 Handling an interrupt (`serviceInterrupt()`)

1. **Read `INT1_SRC` and `INT2_SRC`.** They tell you which condition fired and also release the latched INT1 pin. Nothing happens unless an IA bit is set and the axis/direction bit matches what is armed.
2. **Wake (interrupt 2):** `RepCounter::onWake(now)` starts a session without counting or taking a set sequence. The rep check begins at rest, the sampler starts, and so does the recording (§3.7). A wake that no dip follows within 5 s expires quietly.
3. **Count phase:** call `RepCounter::onCountEvent(now)`. If it counts the dip, the dip goes to the rep check (`RepValidator::onCandidate`). Whenever the check counts reps, now or later (from its 50 Hz samples), the beacon:
   - builds the iBeacon frame with the checked count;
   - **starts a burst with the red LED on**; `loop()` stops the burst, and the LED, 150 ms later;
   - schedules the repeat burst 1.4 s later.

   In lever and paper mode, and with `chk off`, the dip counts at once, as before.
4. **Re-arm phase:** call `RepCounter::onRearmEvent(now)`, which starts the hold-off.
5. **`syncArm()`**, which also arms or disarms the wake.

### 5.4 The re-arm invariant (`syncArm()`)

The pin is **latched** and the ISR triggers on a **rising edge**. If INT1 is already HIGH, for example latched before a reset or set while reconfiguring, **no edge will ever come** and the beacon would go silent.

So every arm or disarm, in every code path, ends with these steps:
1. `INT1_CFG = 0` → write THS and DUR → write `INT1_CFG` (inside `arm()`)
2. read `INT1_SRC` (clears the latch)
3. `xSemaphoreTake(sem, 0)` (forget the edge that the old phase may have produced)
4. `digitalRead(D5)`: if it is HIGH, handle it immediately (`g_serviceNow`)

The housekeeping tick also checks the pin every 2 s, as a safety net.

### 5.5 What goes on the air

Each burst sends a 30-byte advertising payload:

| Bytes | Value | Meaning |
|---|---|---|
| 0–2 | `02 01 06` | AD "flags": LE only, general discoverable |
| 3–4 | `1A FF` | AD length 26, type 0xFF = manufacturer specific data |
| 5–6 | `4C 00` | company ID 0x004C, the iBeacon convention |
| 7–8 | `02 15` | iBeacon type, 21 bytes follow |
| 9–22 | `6D 1E 3F 2A 9B 47 4C 85 A0 E2 7F 31 C5 58` | UUID part 1: "this is a GetMeFit beacon" |
| 23–24 | `00 01` | UUID part 2: **equipment type** (1 = Leg Curl) |
| 25 | e.g. `8F` | major high byte: **set sequence** number (1–255) |
| 26 | `01` | major low byte: **machine instance** (#1) |
| 27–28 | e.g. `00 07` | **minor = rep count** (7) |
| 29 | `C9` | measured power at 1 m (−55 dBm), informational |

So the full UUID is `6D1E3F2A-9B47-4C85-A0E2-7F31C558` + `0001`. `gym::encodeIBeacon()` builds the 25 bytes from offset 5. The gateway's `gym::findIBeaconInPayload()` walks the AD structures and decodes them back. Both are unit-tested, so the two boards cannot disagree. A phone app such as **nRF Connect** shows it as an iBeacon with major `0x8F01` and minor 7.

**Burst timing:**
1. Stop advertising if it is running.
2. Set the data.
3. Start with a 1 s safety timeout.
4. Return at once. `loop()` stops the burst 150 ms later, which covers 2 events at 100 ms intervals. The burst must not block: a rep is counted at the top of the lift, exactly when the weight recorder needs its samples.

The radio is completely off between bursts. Per rep that is 2 bursts × 2 events. Every packet carries the *total*, so a lost packet costs nothing unless it is the last one; the repeat burst covers that case.

**Status frame (battery level).** The iBeacon payload uses 30 of a legacy advertisement's 31 bytes, so the battery voltage travels in a second, separate advertisement:

| Bytes | Value | Meaning |
|---|---|---|
| 0–2 | `02 01 06` | AD flags, as above |
| 3–4 | `0E FF` | AD length 14, manufacturer-specific data |
| 5–6 | `FF FF` | company ID 0xFFFF: the Bluetooth SIG's ID for tests and prototypes (a shipping product needs its own) |
| 7–10 | `6D 1E 3F 2A` | first 4 bytes of the GetMeFit UUID: "this is GetMeFit" |
| 11 | `01` | frame kind: status |
| 12–13 | `00 01` | equipment type (1 = Leg Curl) |
| 14 | `01` | machine instance (#1) |
| 15–16 | e.g. `0F 48` | battery voltage in mV (3912) |
| 17 | `00` / `01` | flags. Bit 0 means USB power is present, so the reading is the charger's voltage and not the battery's. |

It is not an iBeacon, so phones and other iBeacon scanners never mistake it for a rep count. `gym::encodeStatus()` and `gym::findStatusInPayload()` build and decode it, and both are unit-tested like the iBeacon.

- **When:** once at boot, then every 5 min (`BEACON_STATUS_INTERVAL_MS`). It waits while a set is active or a rep burst is on the air or pending, so it never replaces a rep count. A rep counted during a status burst replaces the status burst instead.
- **What:** the Feather's `VBAT` pin goes through an on-board 1:2 divider to A6 (`src/beacon/battery.cpp`). The beacon reads it 8 times against the 3.0 V internal reference, which doesn't sag with the battery. Each read uses a 10 µs acquisition time, because the divider is a ~50 kΩ source and the default 3 µs reads low. The radio is idle during the reading.
- **No LED.** The red LED stays rep feedback.
- On the console, `s` shows `battery 3.92 V (66 %), reported every 300 s`, and each status burst prints `[battery] 3.92 V (66 %), sent to the gateway`. On USB power both say that the reading is the charger's.

### 5.6 Set end and the drift check

`RepCounter::poll()` ends the set 5 s after the last motion event:
- the rep check decides what is still open: a lone first cycle counts, a last lift that never came back down does not. A count decided only now is still broadcast;
- reps go back to 0, as in the paper;
- the monitor prints `[set] done: N reps`, or `[set] done: N reps (M moves ignored)`;
- with a magnetometer fitted, the weight estimate follows ([weight-sensing.md](weight-sensing.md));
- the drift check runs (§3.6).

The beacon does not advertise "0". The gateway works out that the set ended from the silence (§8.3).

---

## 6. Power: what to expect

The paper measured **30 µA** asleep and **40 µA** on average while in use, on a bare module with a coin cell. The Feather cannot get there, and that is expected:

| Consumer | Approx. current |
|---|---|
| Feather's 3.3 V regulator (AP2112K) quiescent current | ~55 µA |
| Battery-voltage divider on the Feather (VBAT → A6) | ~14 µA at 4.2 V |
| GPIOTE input channel used by `attachInterrupt` | ~20 µA |
| LIS3DH, 50 Hz normal mode | ~11 µA |
| nRF52840 System ON sleep + RTC | a few µA |
| **Total while idle** | **~100 µA** |

On the 400 mAh LiPo that is **roughly 5–6 months** idle. Not counted: the green power LED on each Adafruit breakout (about 0.1–0.2 mA each), which is more than all of the above. Remove it for battery use ([weight-sensing.md](weight-sensing.md) §3.3). Advertising adds very little, because the radio is on for ~150 ms twice per rep. The battery status burst every 5 min adds roughly 0.2 µA on average. Treat this as an estimate: measure it with a USB power meter or a multimeter in the battery lead before you rely on it.

Things that cost power, and their switches:
- **Red LED per rep:** `BEACON_LED_FEEDBACK 0`.
- **USB connected:** the console wakes the CPU every 50 ms, but only while USB power is present.
- **Bluetooth console:** connectable advertising every 500 ms for 10 min after power-up and after a phone disconnects; a connected phone adds its connection events and the 50 ms console tick. `BEACON_BLE_CONSOLE 0` turns it off.
- **Streaming:** runs at 50 Hz.

Going lower would mean a bare module like the paper's.

---

## 7. Tuning (bench first, then the machine)

The defaults are starting points: every machine and lifter is different. The beacon has a console for tuning live, on USB and on a phone over Bluetooth (below). On USB, open `pio device monitor -e beacon` and type:

| Command | Effect |
|---|---|
| `h` | help |
| `s` | status: mode, axis, resting reading, both thresholds, phase, reps, set seq, INT1 pin level, and the rep check's settings with what it ignored in the last set |
| `st` | toggle streaming of Teleplot lines: `>a:` (\|a\| on the motion axis), `>cnt:` and `>rearm:` (thresholds), `>int:` (INT1 pin), `>reps:` (dips counted), during a session the rep check's `>pos:` (stack height, mm), `>vel:` (leaky velocity, m/s) and `>counted:`, and with a magnetometer the field in µT: `>tx: >ty: >tz:` (TMAG5273), `>lx: >ly: >lz:` (LIS2MDL) |
| `chk on\|off` | the rep check (§3.7). `off` counts every dip, as before, and logs `[move] would ignore: …` for what the check would drop. It takes effect from the next set |
| `travel <mm>` | rep check: smallest travel up and back (default 80) |
| `match <pct>` | rep check: % of the median travel of the set's counted reps a rep must reach (default 50) |
| `mode stack\|paper\|lever` | load that mode's preset, then recalibrate |
| `dl <mg>` / `dh <mg>` | count / re-arm delta |
| `durl <n>` / `durh <n>` | count / re-arm duration in samples (20 ms each) |
| `hold <ms>` | hold-off after the re-arm event |
| `minint <ms>` | minimum time between two counted reps |
| `axis x\|y\|z\|auto` | motion axis, then recalibrate |
| `cal` | recalibrate (keep the beacon still) |
| `reset` | end the current set now |
| `cfg` | print the current settings as `#define` lines for `beacon_config.h` |
| `mag`, `teach`, `weights`, `forget`, `dump`, `log` | weight sensing, see [weight-sensing.md](weight-sensing.md) §7 |

### On a phone (Bluetooth)

The same console runs over Bluetooth, so the beacon can be checked, tuned and taught with no cable (`ble_console.*`). It uses the Nordic UART Service, which most "BLE UART" apps speak.

1. Install **Bluefruit Connect** (Adafruit, iOS and Android). nRF Connect and Serial Bluetooth Terminal work too.
2. Power the beacon on, or unplug its LiPo and plug it back in. For 10 minutes it advertises as **GetMeFit Leg Curl #1**; the name follows `BEACON_EQUIPMENT_TYPE` and `BEACON_MACHINE_INSTANCE`.
3. Connect to it and open **UART**. The beacon sends its banner and status.
4. Type commands as on USB. Each message the app sends counts as one line, so its end-of-line setting does not matter.

What to know:
- Everything the console prints also goes to the phone: `[rep]`, `[set] done`, `[weight]`, `[cal]` and every reply. CSV dumps (`dump`, `log`) and the Teleplot stream are too long for the link and stay on USB.
- The rep and status bursts go on while a phone is connected, so the gateway still counts. The beacon asks for a 30–60 ms connection interval with short (7.5 ms) connection events.
- Output never waits for the phone. Up to 4 KB queues and a separate task sends it; anything beyond that is dropped, and the phone is told.
- `s` says where the console stands: `bluetooth console: phone connected`, `advertising for 420 more s`, or `not advertising (window over ...)`. A connected phone stops the advertising, so a second phone, or an app that reconnects by itself, keeps others from finding the beacon.
- After a phone disconnects, the beacon advertises for another 10 minutes. Later than that, unplug and replug the LiPo. `BEACON_BLE_CONSOLE_WINDOW_S` sets the window (0 = whenever no phone is connected), and `BEACON_BLE_CONSOLE 0` turns the console off.
- While it advertises, anyone in range can connect: there is no pairing. Keep the window short in a shared gym.
- With USB and a phone both connected, both see every reply, but each sees only the commands typed on it.

### Seeing the signal (Teleplot)

1. Install the **Teleplot** extension in VS Code.
2. In the PlatformIO monitor type `st`, then **close the monitor**. Only one program can open the port at a time, and streaming stays on in the firmware.
3. In Teleplot, connect to the Feather's COM port at 115200.

You'll see `a` move around `rest`, the two threshold lines, `int` pulse when an interrupt fires, and `reps` step up. During a session `pos` draws the stack's height as the rep check sees it: one up-and-back per rep. To type commands again, disconnect Teleplot and reopen the monitor.

### The rep check's lines

Each move the check drops prints one line once it is decided, on USB and on the phone:

```
[rep] 2  (set seq 143; reps 1-2 counted together once they matched)
[rep] 3  (set seq 143)
...
[move] ignored: no rise (the stack was going down) (up 0 mm, back ? mm)
[set] done: 10 reps (2 moves ignored)
```

The other reasons are `no return` (setup), `too small`, `part of the next lift`, `did not match the next rep` and `beacon tilted 62 deg`. Up and back are the rise and return in mm, `?` when unknown.

### Procedure

1. **Bench:**
   1. Lay the breadboard flat, then `s`: it should say `axis auto -> z`, rest ≈ 1000 mg.
   2. Lift it ~30 cm, pause, and lower it; repeat with and without pauses.
   3. Each cycle should add exactly one rep. The first count shows at the second cycle (0 → 2, §3.7).
   4. Rep check: lift and hold for 3 s, then lower it slowly → `[move] ignored: no return`. From a few cm, let it drop onto something soft → `no rise`. Turn it upside down → `beacon tilted`.
2. **Mount on the machine:**
   1. Fix the breadboard rigidly to the top plate / selector stem. Zip ties around a 3D-printed or wooden carrier work; tape alone is not enough.
   2. Strain-relieve the LiPo and the jumpers.
   3. With the stack at rest, type `cal`.
3. **Do reps:** slow, fast, with a pause at the top, and letting the stack touch down.
   - **Missed reps**: `a` doesn't reach `cnt`. Lower `dl`, e.g. `dl 48`. Don't go below 32: one sensor step is 16 mg.
   - **Double counts**: raise `dl` or `durl`. If the extra count comes from bottom bounces, raise `hold`.
   - **Count happens but the next rep is missed**: the rise didn't reach `rearm`. Lower `dh`.
4. **Save:** `cfg`, paste the lines into `src/beacon/beacon_config.h`, rebuild and upload.

---

## 8. Gateway firmware tour (`src/gateway/`)

| File | Role |
|---|---|
| `gateway_config.h` | known machines, timings, upload settings |
| `secrets.example.h` → `secrets.h` | Wi-Fi, Firebase and UREC credentials (git-ignored) |
| `ble_scanner.*` | continuous NimBLE scan → FreeRTOS queue |
| `wifi_link.*` | non-blocking Wi-Fi connect/reconnect (also WPA2-Enterprise) |
| `firebase_client.*` | Firebase REST: sign-in, PATCH/PUT |
| `urec_client.*` | UREC Live REST: GET/PUT machine status |
| `status_led.h` | onboard RGB LED flashes |
| `main.cpp` | the loop that ties it together |

### 8.1 Scanning without missing packets

`ble_scanner::begin()` starts a NimBLE **passive** scan that never ends. Interval = window = 100 ms, i.e. listening 100% of the time, which is what Espressif recommends when Wi-Fi shares the radio. Two settings matter:

- **`setScanCallbacks(&cb, true)`:** `true` means "report duplicates". By default the Bluetooth controller reports each device address *once* per scan. A beacon whose minor changes from 3 to 4 would then never be heard again.
- **`onResult()` runs in NimBLE's own task:**
  1. It walks the raw payload in place with `gym::findIBeaconInPayload()`, then with `gym::findStatusInPayload()` (the battery frame, §5.5).
  2. It ignores anything that is neither, such as phones and other beacons.
  3. It copies `{kind, type, instance, setSeq, reps, batteryMv, flags, rssi, ms}` into a 64-entry FreeRTOS queue.

  Nothing blocking ever happens there.

`loop()` empties the queue. While `loop()` is busy with an HTTPS request, the scan keeps running and packets wait in the queue. If the queue ever fills, the callback compacts it to the **newest packet per beacon and set**, plus the newest status frame per beacon. Counts are cumulative, so the newest packet carries everything the older ones did. This way neither a busy beacon nor one that has just gone quiet loses its final count. The ESP32-C3 shares one radio between Wi-Fi and BLE by time slices; the default Wi-Fi modem sleep must stay on for that, so the code never calls `WiFi.setSleep(false)`.

### 8.2 Whitelist

`kMachines[]` maps each beacon to a machine:

```cpp
{gym::kEquipLegCurl,      1, "LC001", "Leg Curl Machine 1"},
{gym::kEquipLegExtension, 1, "LE001", "Leg Extension Machine 1"},
{gym::kEquipLatPulldown,  1, "LP001", "Lat Pulldown 1"},
{gym::kEquipSeatedRow,    1, "SR001", "Seated Row 1"},
```

A beacon is identified by its equipment type (from the UUID) and its instance (low byte of major). The `code` is the machine's code in the UREC Live backend; the backend's `DataInitializer` seeds LC001, LE001, LP001, SR001 and others. The code is also the machine's key in Firebase. A GetMeFit beacon that isn't listed is logged once and ignored.

For a second beacon:
1. Give it a different `BEACON_EQUIPMENT_TYPE`/`BEACON_MACHINE_INSTANCE` in its `beacon_config.h`.
2. Make sure there is a matching row here.

### 8.3 The machine tracker (the paper's "repetition vector")

`gym::MachineTracker` (`lib/gym_core/src/machine_tracker.cpp`) keeps one slot per machine. Its rules:

- **New set:** only when the beacon's **set sequence number changes**, or its rep count goes **down**.
- **Same set, higher count:** update the live count.
- **No packet for 8 s** (`GATEWAY_SET_GAP_MS`): the set is **finished**.
  - The live count drops to 0, matching the paper's dashboard graph that falls back to 0.
  - A set record is emitted.
  - The beacon itself resets after 5 s; the extra 3 s cover a lost last packet.
- **A late packet of a finished set** (same sequence number) **reopens and amends** it. It is re-recorded under the same key, so it overwrites rather than duplicates.
- **"In use":** becomes true once a set reaches **2 reps**, and false after **5 minutes** without packets.
  - A single "rep" is usually someone moving the selector pin, so 1-rep sets are neither "in use" nor recorded.
  - 5 minutes covers normal rest between sets.
- **Uploads:** a machine is only marked for upload when the count, set, in-use flag or last-set reps change. Signal strength alone doesn't count.
- **Battery** (status frames, `onStatus()`):
  - A status frame only updates the battery fields. It never counts as a packet for the 8 s set gap or the 5 min idle time, so a beacon's heartbeat cannot keep a set open or a machine "in use".
  - A reading is reported, which means printed and uploaded:
    - the first time;
    - when USB power comes or goes;
    - when it has moved 20 mV from the last report (`GATEWAY_BATTERY_STEP_MV`), so ADC noise doesn't cause uploads;
    - and at least every 30 min (`GATEWAY_BATTERY_REFRESH_MS`), so `battery/updatedAt` shows whether the reading is current. A beacon whose `updatedAt` is much older than 30 min has stopped transmitting, most likely because its battery is flat.
  - The percentage comes from `gym::batteryPercent()`, a typical LiPo resting-voltage curve. Treat it as a guide only (±10 %).

Every change prints the vector:

```
[reps] LC001=7 (set 3, in use, -61 dBm) | LE001=0 | LP001=0 | SR001=0
[set] LC001 set 3: 10 reps in 31.2 s
[battery] LC001 3.92 V (66 %)
```

### 8.4 Firebase

The gateway writes three things under `/gym`.

**1. Live state**, as the paper did, as one **multi-path PATCH** of every changed machine, at most twice a second:

```http
PATCH https://<db>/gym/machines.json?print=silent&auth=<token>
{"LC001/name":"Leg Curl Machine 1","LC001/type":"Leg Curl","LC001/reps":7,"LC001/setNumber":3,
 "LC001/inUse":true,"LC001/lastSetReps":10,"LC001/rssi":-61,"LC001/updatedAt":{".sv":"timestamp"}}
```

- Slash-separated keys update only those fields; other machines are untouched.
- `{".sv":"timestamp"}` asks Firebase to fill in its own clock, because the gateway has no clock.
- `print=silent` makes the reply an empty `204`.
- **The beacon's battery** goes in the same PATCH, but only when a reading is reported (§8.3). It lives in its own child, so `battery/updatedAt` is the time of the reading, not of the last rep:

  ```http
  {"LC001/battery/mv":3920,"LC001/battery/pct":66,"LC001/battery/usb":false,
   "LC001/battery/updatedAt":{".sv":"timestamp"}}
  ```

  `usb: true` means the beacon was on USB power: `mv` and `pct` then show the charger's voltage, not how full the battery is.

**2. Finished sets:**

```http
PUT https://<db>/gym/sets/LC001/<bootId>_<setNumber>.json?print=silent&auth=<token>
{"reps":10,"setSeq":143,"durationMs":31200,"endedAt":{".sv":"timestamp"},"gateway":"gw-01"}
```

- `bootId` is random per gateway boot, so keys never collide.
- PUT to a fixed key is **idempotent**: a retry or an amended set overwrites instead of duplicating.
- `endedAt` is the upload time, which is within seconds of the set's end unless Wi-Fi was down.

**3. Heartbeat** every 60 s at `/gym/gateways/gw-01`: uptime, IP, Wi-Fi RSSI, free heap, and packet/drop counters. If the dashboard is quiet, this tells you whether the gateway is alive.

**Auth:**
- When `FIREBASE_EMAIL` is set, the gateway signs in as that Firebase user. It POSTs to `identitytoolkit.googleapis.com/v1/accounts:signInWithPassword` and gets a 1-hour ID token.
- The token is added as `?auth=` and renewed 10 minutes early.
- After a `401` on a token older than 30 s, the token is renewed once and the request retried once.
- A `401` on a fresher token cannot be an expiry. It means "your rules deny this", so it is only reported.
- Sign-in is attempted at most once every 30 s, whatever triggers it.
- The database connection is kept open between requests (keep-alive). Sign-in uses its own short-lived connection, because switching hosts would close the kept-open one.

**Failures:** live updates retry every 5 s. Finished sets queue (up to 16) until they are uploaded.

### 8.5 UREC Live machine status

The backend endpoint is `PUT /api/machines/code/{code}/status` with `{"status":"In Use"|"Available"}`. Every PUT is saved, broadcast to every app over WebSocket, and logged as a CHECK_IN/CHECK_OUT in the admin analytics, **even if nothing changed**. QR check-ins use the same endpoint. So the gateway is careful:

- **Only on changes.** "In use" turning true or false triggers one sync, nothing else.
- **Claim.** When a machine becomes in use, the gateway GETs `/api/machines/code/{code}` first. Only if the status is `"Available"` does it record that it **claimed** the machine, then PUT `"In Use"`. The claim is stored in flash (`Preferences`), so it survives a reboot. If the machine is already `"In Use"`, for example from a QR check-in, the gateway leaves it alone.
- **Release.** When the machine goes idle, the gateway releases it only if it claimed it, and PUTs `"Available"` only if the status is still `"In Use"`.
- **Out of Order.** The backend answers 403/409 and keeps the machine Out of Order; the gateway accepts that and stops.
- **Claim first, then PUT.** The gateway records the claim *before* sending `"In Use"`.
  - If the PUT reaches the backend but its reply is lost (timeout, dropped connection, 5xx), the next try sees `"In Use"`, treats it as its own, and still releases the machine later.
  - If the PUT certainly did not take effect, the claim is dropped again. That means it was never fully sent (connection refused, send failed) or the backend refused it (3xx/4xx). Otherwise a member's QR check-in made before the retry would look like the gateway's own.
- **Retries back off** 10 s, 20 s, 40 s … up to 5 minutes:
  - Marking a machine "In Use" is best effort: it gives up after 3 tries in total.
  - **A release never gives up.** A machine the gateway marked "In Use" must not stay that way because the backend was briefly unreachable, e.g. a Railway redeploy.
- **Unknown codes.** An unknown machine code makes the backend return 500. That is why the gateway checks every configured code once after Wi-Fi first connects, and logs any it can't find.
- **After a reboot.** Claims made before the reboot are released after the 5 min idle time unless reps arrive.

`UREC_BASE_URL` is **empty by default**, which disables all of this. Test against a local backend before pointing it at production (§10).

### 8.6 LEDs

| Board | Color | Meaning |
|---|---|---|
| Gateway | blue blink | beacon packet received |
| Gateway | green | upload OK |
| Gateway | red | upload or network error |
| Beacon | red flash | rep counted (during the radio burst) |
| Beacon | two quick red blinks every 2 s | not calibrated: moving, the axis isn't vertical (lever mode: isn't horizontal), or the LIS3DH isn't answering |

---

## 9. Firebase setup

1. **Create a project:** <https://console.firebase.google.com> → **Add project** (e.g. `getmefit`; Analytics not needed).
2. **Create the database:** **Build → Realtime Database → Create database**. Pick a location and start in **locked mode**.
   - Copy the database URL shown at the top. It is either `https://<name>.firebaseio.com` (US) or `https://<name>.<region>.firebasedatabase.app`.
3. **Create the gateway's user:**
   1. **Build → Authentication → Get started → Sign-in method → Email/Password → Enable.**
   2. **Users → Add user**, e.g. `gateway@getmefit.dev` with a long random password.
   3. Copy the user's **UID**.
4. **Get the API key:** **Project settings (gear) → General → Web API key.** If it's missing, add a Web app (`</>`) under "Your apps" and use its `apiKey`. This key is not secret; it only identifies the project.
5. **Set the rules:** **Realtime Database → Rules**. Paste `docs/firebase-database.rules.json`, replace `REPLACE_WITH_GATEWAY_UID` with the UID, and **Publish**.
   - Anyone can *read* `/gym`; it holds only equipment usage, no personal data, as in the paper.
   - Only the gateway can write.
6. **Fill in `secrets.h`** (next section).

*Quick-and-dirty alternative:* create the database in **test mode** and leave `FIREBASE_EMAIL` empty. Anyone can then read and write, and the rules expire after about a month, after which every write fails with 401. Fine for one afternoon, not for a demo.

---

## 10. Secrets, Wi-Fi and the backend

Copy `src/gateway/secrets.example.h` to `src/gateway/secrets.h`. It is git-ignored. Without it the gateway still builds, prints a `#warning` per file, and runs serial-only.

```cpp
#define WIFI_SSID "MyPhoneHotspot"
#define WIFI_PASSWORD "..."
#define FIREBASE_DB_URL "https://getmefit-default-rtdb.firebaseio.com"
#define FIREBASE_API_KEY "AIza..."
#define FIREBASE_EMAIL "gateway@getmefit.dev"
#define FIREBASE_PASSWORD "..."
#define UREC_BASE_URL "http://192.168.43.20:8080"
```

### Wi-Fi

- The ESP32-C3 only does **2.4 GHz**.
- University networks are usually WPA2-Enterprise (eduroam) or have a login page. A login page will not work at all.
- **The easiest option is a phone hotspot.** On iPhone, turn on *Maximize Compatibility* to force 2.4 GHz.
- For eduroam-style PEAP, fill in `WIFI_EAP_USERNAME`/`WIFI_EAP_PASSWORD` (and `WIFI_EAP_IDENTITY` if your IT asks for an outer identity); `WIFI_PASSWORD` is then ignored.

### UREC Live backend

- **Local:** run `mvn spring-boot:run` in `urec-live-backend`, which listens on port 8080 on all interfaces.
  1. Find your PC's IP with `ipconfig`.
  2. Put `http://<that IP>:8080` in `UREC_BASE_URL`.
  3. Allow port 8080 through Windows Firewall.
  4. Keep the PC and the gateway on the same network.
- **Production:** `https://urec-live-backend-production.up.railway.app`. Railway requires HTTPS.
  - Every status change appears in the real admin analytics as an *anonymous* CHECK_IN/CHECK_OUT.
  - Use it only once you've tested locally.

### TLS

HTTPS certificates are checked against the CA bundle built into the ESP32 core, which covers Google and Let's Encrypt. If a handshake fails on the bench, `GATEWAY_TLS_INSECURE 1` in `gateway_config.h` skips the checks. Never use that on shared Wi-Fi.

---

## 11. Build, flash, test

Use VS Code's PlatformIO sidebar (*Project Tasks → beacon / gateway → Upload / Monitor*) or a terminal in `GetMeFit/`.

| | Beacon (Feather nRF52840) | Gateway (ESP32-C3-DevKitM-1) |
|---|---|---|
| Build | `pio run -e beacon` | `pio run -e gateway` (the first build downloads the ESP32 toolchain and takes several minutes) |
| Upload | `pio run -e beacon -t upload` | `pio run -e gateway -t upload` |
| Monitor | `pio device monitor -e beacon` | `pio device monitor -e gateway` |
| If upload fails | double-click the Feather's reset button. The red LED pulses and a `FTHR840BOOT` drive appears; upload again | install the Silicon Labs **CP210x** driver; or hold **BOOT**, tap **RESET**, release BOOT |
| Size today | 122.6 KB RAM, 210 KB flash (99 KB of the RAM is weight sensing, [weight-sensing.md](weight-sensing.md) §6.4; about 7 KB is the rep check) | 33 KB RAM, 1.01 MB flash (3 MB app partition, `huge_app.csv`) |

With both boards plugged in, `pio device list` shows the ports. Un-comment `upload_port`/`monitor_port` in `platformio.ini` if auto-detect picks the wrong board. The environment names changed from `adafruit_feather_nrf52840` to `beacon`/`gateway`. Your old `.pio/…/adafruit_feather_nrf52840` folders can be deleted, and VS Code regenerates `launch.json` itself.

**Expected beacon output.** The first lines appear as soon as the monitor connects:

```
GetMeFit beacon ready: Leg Curl #1. Type h for commands.
Leg Curl #1, tx +4 dBm
mode stack, axis auto -> z, rest 1008 mg (x 16, y -32, z 1008)
  count: |a| < 944 mg for 4 samples (80 ms)
  rearm: |a| > 1072 mg for 2 samples (40 ms)
  hold-off 350 ms, min rep interval 600 ms, set timeout 5000 ms
phase count, reps 0, set idle (seq 142), INT1 pin 0
rep check: on, travel >= 80 mm and >= 50 % of the set's reps, wake at rest + 48 mg
weight: TMAG5273 at 0x35, 0 weights taught (0 reps); LIS2MDL at 0x1E, 0 weights taught (0 reps)
[rep] 2  (set seq 143; reps 1-2 counted together once they matched)
[rep] 3  (set seq 143)
...
[move] ignored: no rise (the stack was going down) (up 0 mm, back ? mm)
[set] done: 10 reps (1 moves ignored)
```

**Expected gateway output:**

```
GetMeFit gateway gw-01 (boot 3fa2c1d0)
  Leg Curl #1 -> LC001 (Leg Curl Machine 1)
  ...
  Firebase: https://getmefit-default-rtdb.firebaseio.com, UREC: http://192.168.43.20:8080
[wifi] connecting to "MyPhoneHotspot"
[reps] LC001=0 | LE001=0 | LP001=0 | SR001=0
[wifi] connected, IP 192.168.43.57, RSSI -48 dBm
[urec] LC001 = "Leg Curl Machine 1" (Available)
[urec] LE001 = "Leg Extension Machine 1" (Available)
[urec] LP001 = "Lat Pulldown 1" (Available)
[urec] SR001 = "Seated Row 1" (Available)
[firebase] signed in as gateway@getmefit.dev
[reps] LC001=1 (set 1, -58 dBm) | LE001=0 | LP001=0 | SR001=0
[reps] LC001=2 (set 1, in use, -58 dBm) | LE001=0 | LP001=0 | SR001=0
[urec] LC001 -> In Use
...
[set] LC001 set 1: 10 reps in 31.2 s
```

### Unit tests

`test/test_gym_core/` holds 78 Unity tests for the packet format, the rep counter, the rep check, the tracker, the weight sensing ([weight-sensing.md](weight-sensing.md) §11) and the set store. The rep-counting tests cover the tricky cases: pause at the top, bounce during the hold-off, slow tempo, max reps, `millis()` wrap-around, the wake, set amend after a late packet, bumps not recorded, and the full set ring.

The rep check's tests drive the real `RepCounter` and `RepValidator` with synthetic sets. A scripted stack height (moves, holds, a free-fall drop) becomes what the LIS3DH reads: gravity plus the stack's acceleration along a tilted, rocking or flipped beacon, 3 mg of noise, 4 mg steps and ±2 g clipping. An emulated comparator stands in for the count, re-arm and wake interrupts. The scenarios: 8 reps from rest; lat pulldown with setup, hold, 10 reps and a let-go or a stand-up; setup merged into rep 1; a 12 cm fidget; partial reps at 55 %; 3 cm knocks; a 3.5 s pause at the bottom; the wake missed; a single cycle; the 0 → 2 → 3 timing; 5 mg of bias; ±17° rocking; a flipped beacon.

**Replaying recorded sets.** `tools/replay_reps/run_replay.cmd` builds `replay_reps.cpp` with MSVC (VS 2022 Build Tools) and runs downloaded set CSVs through the same `RepValidator`. It prints, per set, the dips counted then, the count now and each dip's verdict; with `--truth` it also prints the error:

```
tools\replay_reps\run_replay.cmd sessions\2026-11-11_1413_sets --truth=tools\replay_reps\truth_2026-11-10.txt
```

`tools/replay_reps/rep_validator_proto.py` is the Python prototype the check was first tuned with (plots with `--plot=<dir>`). The C++ has since gained the stop rule (§3.7, step 3).

Run them on a plugged-in board:

```
pio test -e gateway     # recommended: its USB-serial chip makes Unity output reliable
pio test -e beacon      # also works on the Feather
```

This **replaces the firmware** on that board, so upload the normal firmware again afterwards. `pio test -e gateway --without-uploading --without-testing` only checks that the tests compile.

---

## 12. Testing at the gym (mirroring the paper's Table II)

The paper did 3 sets of 10 reps on leg curl, leg extension and lat pulldown. It compared the true count with the dashboard (90.5%) and the watch (94.6%). To do the same:

1. Mount the beacon (§7), tune it from the phone console (§7, "On a phone"), and put the gateway within ~10 m. Check the RSSI in the `[reps]` line: −85 dBm or better is comfortable.
2. Do 3 sets × 10 reps per machine. Include **one set with a 1–2 s pause at the top** and **one very slow set**. Those break single-trigger counters, and the slow set shows whether `dl 48` is needed (§3.2).
3. For each set, record the truth, the beacon count (`[rep]` lines, on the phone), the gateway's set record, and Firebase's `/gym/sets`.

| Machine | Set | Truth | Beacon | Gateway | Firebase | Notes |
|---|---|---|---|---|---|---|
| Leg curl | 1 | 10 | | | | normal |
| Leg curl | 2 | 10 | | | | pause at top |
| Leg curl | 3 | 10 | | | | slow |

4. For a direct comparison, repeat one set with `mode paper` to see the single-trigger behavior the paper used.

---

## 13. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `[err] no LIS3DH at 0x18` | SDA/SCL swapped or loose (wires 7/8), sensor not powered (wires 5/6). Run the schematic §6 checks. |
| `[cal] failed: motion axis is not vertical` | No axis has 700 mg of gravity: the board is mounted at an angle. Mount it level, or use `mode lever` with an explicit axis. |
| `[cal] failed: beacon is moving` | Keep it still for a second; it retries every 10 s. |
| `[err] LIS3DH stopped sampling` or `[cal] failed: no LIS3DH on I2C` | The sensor lost power or dropped off the bus: a loose VIN/GND/SDA/SCL jumper (wires 5–8). The beacon re-initialises it and retries every 10 s. Fix the wiring and strain-relieve the jumpers. |
| `[cal] failed: lever axis is not horizontal` | Lever mode: the chosen axis reads more than 500 mg at rest. Choose the axis that is horizontal at rest and tilts with the lever (`axis x\|y\|z`), then `cal`. |
| Reps never count, `s` shows `INT1 pin 0` and `>int` never pulses | Wire 9 missing or in the wrong hole (H23 → I14). `BEACON_INT_PIN` must be 5. |
| Reps count only while plugged into USB | Check the battery is charged and plugged into the JST socket. The beacon never waits for USB. |
| Double counts | Raise `dl`/`durl`/`hold` (§7). Make the mount rigid: a wobbling breadboard adds its own bumps. |
| Missed reps | Lower `dl`/`dh`. Check that `s` shows the axis you expect. |
| The count stays at 0 for the first rep | Expected: reps 1 and 2 count together once rep 2 is back down (§3.7). |
| `[move] ignored: …` for a real rep | `too small`: lower `match` or `travel`. `beacon tilted`: make the mount rigid. To compare, do one set with `chk off`: it counts every dip and logs what the check would drop. |
| A setup or a let-go still counts | Look at its `[move]` line, or `s` after the set. With `st` on, `pos` should show one up-and-back per rep. |
| Gateway sees nothing | Check `[ble]` lines and that the beacon is in `kMachines[]`. A phone with **nRF Connect** should show the iBeacon; if it doesn't, the beacon isn't advertising. |
| Gateway sees the beacon, Firebase stays empty | Look for `[firebase] … -> 401` (rules or UID) or `sign-in failed` (API key, email/password provider not enabled). Use the exact region URL. |
| `[urec] LC001: HTTP 500` at boot | That machine code doesn't exist in that backend. Fix `kMachines[]`. |
| `[urec] … HTTP -1` | Backend unreachable: wrong IP, firewall, or a different network. |
| Monitor or Teleplot can't open the port | Only one program can hold a serial port; close the other. |
| Upload to the Feather fails | Double-click reset (bootloader mode), then upload. |
| `[weight] …` messages | See [weight-sensing.md](weight-sensing.md) §12. |

---

## 14. Weight sensing

Weight sensing is implemented: a magnetometer on the LIS3DH's STEMMA QT chain, read while a set is active, reproducing W8-Scope. [weight-sensing.md](weight-sensing.md) explains it.

What is still open is **sending the weight on**: the estimate is only printed on the beacon's console. The iBeacon frame is full. Major and minor are used, and a legacy advertisement has 31 bytes. The options are:
- put the weight in a second AD structure next to the iBeacon. There is 1 byte left, so this would mean dropping the flags or switching to a custom manufacturer format with the Nordic test company ID;
- use BLE 5 extended advertising, which both the nRF52840 and the ESP32-C3 support;
- make the beacon scannable and put the weight in the scan response; the gateway would then need an *active* scan;
- send a separate set-summary advertisement after each set. The battery status frame (§5.5) already works this way, and its `kind` byte leaves room for a set-summary kind that carries the set sequence and the weight.

On the gateway side, the weight would go into `BeaconPacket`, `MachineState` and `SetRecord`, then into the Firebase JSON. The backend's `WorkoutSet` already has `reps` and `weightLbs`.
