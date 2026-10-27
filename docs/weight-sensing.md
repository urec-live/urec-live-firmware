# Weight Sensing: How It Works

This guide explains how the beacon estimates the weight lifted in each set, from the physics of a steel weight stack to the console output. It reproduces:

> M. Radhakrishnan, A. Misra, R. K. Balan, *W8-Scope: Fine-grained, practical monitoring of weight stack-based exercises*, Pervasive and Mobile Computing 75 (2021) 101418 ([doi:10.1016/j.pmcj.2021.101418](https://doi.org/10.1016/j.pmcj.2021.101418))

It builds on the rep counter in [rep-counting-walkthrough.md](rep-counting-walkthrough.md) and the breadboard in `../beacon-breadboard-schematic.md`. The estimate is printed on the beacon's serial console only. The radio packet, the gateway and Firebase are unchanged; see §13 for what sending it on would take.

---

## 0. Quick start

1. **Plug in the magnetometer.** Connect the TMAG5273 (Adafruit #6489) to the LIS3DH's spare STEMMA QT socket with a cable (schematic §8). Keep it a few centimetres from the Feather, the LiPo and current-carrying wires.
2. **Flash and check.** Run `pio run -e beacon -t upload`, then `pio device monitor -e beacon`. The status line `s` should now include `weight: TMAG5273 at 0x35, 0 weights taught`.
3. **Mount the beacon on the weight stack's top plate**, rigidly, the same way the rep counter needs it (walkthrough §7). The magnetic signature belongs to that machine and that mounting position.
4. **Teach each weight.** Put the pin at a weight, do a set of at least 4 reps and let the stack rest at the bottom. After the set the beacon prints `[weight] set 143: 8 reps measured, lift 28 cm - no weights taught yet`. Type `teach 50` (your weight, in the machine's unit). Teach **at least two sets per weight**, one through the full range of motion and one through about half of it (§5).
5. **Lift.** From then on every set ends with `[weight] set 151: ~50 (7/8 reps agree) | TMAG5273, lift 27 cm`.

Every step is explained below.

---

## 1. The paper and this implementation

W8-Scope fixes one small sensor board, an accelerometer plus a 3-axis magnetometer, to the top plate of a weight stack. From the magnetic field and the motion it infers the weight, the exercise, the user and common mistakes. This project reproduces the **weight** part.

| Paper | This project |
|---|---|
| "a simple device, mounted rigidly to the top plate of a weight stack" | the beacon already sits on the stack for rep counting (schematic §8, mode `stack`) |
| "3-axis accelerometer and 3-axis magnetometer sensor streams ... at 50 Hz" | LIS3DH + TMAG5273 (LIS2MDL in the next step), sampled at 50 Hz while a set is active (`src/beacon/weight_sensing.cpp`) |
| "the magnetic field strength is affected by this weight" | field change at the top of each lift, relative to the field at rest (`lib/gym_core/src/weight_pipeline.cpp`) |
| "the raw accelerometer data is initially filtered, then we obtain the local maxima and local minima ... δ (60% of the highest/lowest sample amplitude) ... ΔT (2 s)" | `findValleys()`, `BEACON_W8_MIN_REP_MS 2000` |
| "The segment between two consecutive valleys is assumed to represent a repetition" | one rep per valley-to-valley segment |
| "integrated it using cumulative trapezoidal integration ... low-pass filtered and then integrated again to obtain the displacement" | `integrateSegment()`, see §4 step 5 for the drift correction used instead of an unknown filter |
| "both magnetic and accelerometer sensor data are thus needed to accurately distinguish between different weights" | every rep's features include the lift height |
| "the probability is computed first using each individual repetition, and then combined across the consecutive repetitions" | every rep votes, the set takes the majority (`lib/gym_core/src/weight_model.cpp`) |
| random forest, trained offline | weights taught on the beacon with `teach`; raw sets can be dumped as CSV for an offline forest (§8) |

§10 lists every deviation and the reason for it.

---

## 2. The physics

### 2.1 Why the field depends on the weight

A weight stack is a pile of steel plates. Steel bends the Earth's magnetic field (and any field from the machine's own steel) around itself, so a magnetometer near the stack reads a field that depends on where the plates are.

The beacon sits on the **top plate**. When the lifter pulls with the pin in plate *K*, plates 1..*K* rise together with the sensor and the remaining plates stay down. The paper models the field at the top of the lift as

```
          K    w_i          M      w_i
    B  =  Σ  -------   +    Σ  -----------        (paper eq. 1)
         i=1   d_i²        i=K   (D + d_i)²
```

where *d_i* is a plate's distance from the sensor and *D* the lift height. The first term (plates that moved with the sensor) is unchanged. The second term (plates left behind) gets weaker the higher the stack goes. So **the field at the top of the lift depends on how many plates were lifted**.

At rest all plates are in their usual place whatever the pin setting, so the field at rest is (almost) the same for every weight. The difference between the two is what the beacon measures.

### 2.2 Light weights are easy, heavy weights are hard

The paper's lat-pulldown data (Fig. 8a) shows the mean field per set for 3.75 to 23.75 kg: −137, −171, −210, −223, −236, −249, −253, −257, −254 µT. The steps shrink from 34–39 µT for the light weights to 3–4 µT for the heavy ones. Eq. 1 predicts this "U-shape": each added plate is further from the sensor. Expect the same here: light weights separate well, heavy neighbours much less.

### 2.3 Height and weight can look alike

A heavy weight lifted a little can produce the same field change as a light weight lifted far. The paper's example is 8.75 kg lifted 6 cm versus 13.75 kg lifted 24 cm. That is why the accelerometer's lift height is part of every rep's features, and why the beacon also records the field **5 cm above the bottom of the lift**. That point is reached in every rep, whatever the range of motion, so it depends on the weight and not on how far the lifter goes (as long as consecutive reps are of similar height, §4 step 5).

---

## 3. Hardware

### 3.1 Which sensor the paper used

The paper's device is Dialog's DA14583 IoT Sensor kit. Its magnetometer is a **Bosch BMM150**, a geomagnetic sensor with FlipCore elements on X and Y and a Hall plate only on Z. It has about 0.3 µT rms noise and was used passively, without any magnet. So the paper did not use a "Hall effect sensor" as such. It used an Earth-field magnetometer.

### 3.2 The two magnetometers on this beacon

| | TMAG5273A1 (Adafruit #6489) | LIS2MDL (Adafruit #4488) | BMM150 (paper) |
|---|---|---|---|
| Technology | 3D Hall | AMR | FlipCore X/Y, Hall Z |
| I2C address | 0x35 | 0x1E | – |
| Made for | magnets (±40 mT) | the Earth's field (±4.9 mT) | the Earth's field |
| Resolution | 1.22 µT/LSB | 0.15 µT/LSB | 0.3 µT |
| RMS noise, one sample | 22 µT X/Y, 9 µT Z (32x averaging, low-noise) | 0.3 µT (offset cancellation) | 0.3–1.4 µT |
| Offset | ±300 µT typ., drifts ±3 µT/°C | ±6 µT with offset cancellation | ±40 µT |
| Current while sampling | 3.0 mA converting | 575 µA at 50 Hz | – |
| Between sets | sleep, 5 nA | idle, 1.5 µA | – |

**What this means for the TMAG5273:** its noise is about the size of the paper's weight steps. The beacon squeezes the most out of it:
- every 50 Hz sample averages 4 triggered 32x conversions (`BEACON_TMAG_OVERSAMPLE`), which halves the noise to about 11 µT on X/Y and 4.5 µT on Z;
- every feature averages 5–11 samples;
- the classifier trusts each axis according to its noise (§5);
- only changes from the per-set field at rest are used, never absolute values, because the offset and its temperature drift are larger than the signal.

Expect it to tell light weights apart and to answer `uncertain` for close heavy ones. The LIS2MDL is the closer match to the paper's sensor. The next step on this branch adds it, so both can be compared on the same sets.

### 3.3 The breakouts draw current all the time

Each Adafruit breakout has a voltage regulator and a green power LED (through 10 kΩ, about 0.1–0.2 mA). That is more than the whole beacon's ~100 µA idle budget (walkthrough §6). For a battery-powered beacon, remove the LED (or its resistor) on the magnetometer board, and on the LIS3DH board too.

---

## 4. The pipeline, step by step

Recording starts with the first counted rep and stops when the rep counter ends the set (5 s without motion). Then `gym::analyzeMotion()` and `gym::magFeatures()` (`lib/gym_core/src/weight_pipeline.cpp`) process the recording:

1. **Rest window and gravity.**
   - The set only ends 5 s after the last motion event, so the end of the recording shows the stack at rest.
   - The beacon looks for the first 1.5 s after the last event (+0.5 s) in which every accelerometer axis stays within `BEACON_STILL_P2P_MG`. That is early in the rest, before anyone pulls the pin for the next set.
   - The mean of that window is the gravity vector. The median of the field there is the **field at rest**, and it must not drift between the window's two halves.
2. **Vertical acceleration.** a_up = a · ĝ − |g|. The direction comes from gravity, so a board that is upside down or tilted (by up to the 45° the rep counter allows) works.
3. **Low-pass.** A zero-phase filter at 3 Hz for finding the reps, and a lighter one at 8 Hz for integrating. Each pass starts on a mirror image of the samples at its end, as `filtfilt` does, because the recording begins in the middle of the first rep's dip.
4. **Reps** (paper: valleys).
   - Valleys are local minima below 60 % of the lowest value, at least ΔT = 2 s apart.
   - The 60 % is measured only up to 1 s after the last counted rep, so the bump of the stack landing cannot set it.
   - Between two valleys there must be a peak above 60 % of the highest value (the paper's maxima; also the rep counter's re-arm rule).
   - A pause at the top gives two dips with no peak between them. The first one is kept, so all valleys sit at the same point of the motion.
   - If the rep counter counted clearly more reps than there are valleys, the reps are faster than 2 s. ΔT is then shortened to 60 % of the counted interval (at least `BEACON_MIN_REP_INTERVAL_MS`) and the console says `dT … ms`.
   - A rep the counter counted but whose dip is shallower than the 60 % (often a slower first rep) still gets its valley: the lowest point of its dip.
5. **Lift height.** a_up is integrated twice over one window per rep, from the **fastest moment of one lowering to the fastest moment of the next**. Such a window contains one bottom, one lift and its top. Removing the window's mean acceleration and mean velocity makes the speed and height equal at both ends. This cancels any constant accelerometer bias. At the window edges the speed is at its peak, so a boundary off by a sample hardly matters. The edges are the same point of the motion whether or not the lifter holds at the top or the bottom.
   - Height = highest minus lowest point.
   - Windows with a gap of more than 200 ms between samples are dropped.
   - So are windows lower than 5 cm or lower than 30 % of the median lift; those are bumps and partial reps.
   - So are windows longer than 1.75× the typical rep. Those hold several reps whose tops were too shallow to find (half reps the counter did not count either), and integrating that long gathers too much drift.
   - In the unit tests the heights of 30 cm sets come out within ±1.5 cm, with or without pauses. That matches the paper's ±1.15 cm.
   - **Limit:** equal speed and height at both ends hold for consecutive reps of similar height. That covers steady sets and gradual fatigue. With ±2.5 cm of variation from rep to rep, heights stay within ±1.5 cm and the 5 cm field within ±3.2 µT. Strongly alternating reps (20 cm, 30 cm, 20 cm, …) are off by up to 4 cm.
6. **Field features**, all as change from the field at rest (0.1 µT):
   - at the top of the lift: mean of the 11 samples around the highest point;
   - exactly 5 cm above the bottom on the way up: the 5-sample means on either side of that point, interpolated, so the value does not depend on how fast the stack passed it.
7. **Feature vector** (`kWeightFeatureVersion 1`): `[top x, top y, top z, 5 cm x, 5 cm y, 5 cm z, height mm]`.

The first rep's lift is not recorded: the recording starts when that rep is counted, at its top. A set of N counted reps therefore gives up to N−1 votes.

---

## 5. The classifier

`gym::WeightModel` (`lib/gym_core/src/weight_model.cpp`):

- **Teaching.** `teach <w>` stores every rep of the last set as an example labelled `w`. Up to 160 examples and 24 weights are kept. When the model is full, the oldest example of the most-taught weight makes room.
- **Scaling.** Each feature is divided by the spread seen *within* one weight, but never by less than the sensor's own noise for that feature.
  - For example, the TMAG5273's X/Y noise counts about 2.5× less than its Z.
  - Height has a floor of 15 mm.
  - A feature that never varies cannot divide by zero.
- **Per-rep vote.** Each rep looks at its 3 nearest examples (fewer if fewer are taught). The majority wins, and a tie goes to the closer weight.
  - A rep further from every example than 3× the typical example-to-example distance votes **unknown**.
  - Without that, a model with one taught weight would report it for anything.
- **Set result.** A weight is reported only with at least 3 votes, of which at least 60 % agree. That is the paper's confidence threshold *t* = 0.6. Otherwise the console says `uncertain` or `unknown`.

**Why at least two sets per weight, at full and half range:** the nearest-neighbour vote only knows the heights it was shown. A set lifted much lower than anything taught for its weight can look like a lighter weight (§2.3). The unknown vote then usually answers `uncertain` rather than giving a wrong weight, but teaching both ranges avoids it.

**Why not the paper's random forest:** it needs a large labelled dataset, which does not exist yet. The CSV dump (§8) collects exactly what is needed to train one offline. emlearn can then export it to C for the beacon.

**The taught weights live in flash** (`/w8_tmag.bin` in the Bluefruit file system, about 2.6 KB at most). They survive a reboot. A firmware with a different feature layout ignores the old file and asks you to teach again.

---

## 6. Beacon firmware

| File | Role |
|---|---|
| `weight_settings.h` | `MagSensor` choice for `beacon_config.h` |
| `magnetometer.h` | interface the recorder uses, so it does not care which breakout is fitted |
| `tmag5273_mag.*` | register-level TMAG5273 driver (§6.3) |
| `set_recorder.*` | 50 Hz sample buffer for one set (static ring buffer) |
| `weight_sensing.*` | sensor detection, sampler, set lifecycle, pipeline call, console commands, CSV dump, flash |
| `lib/gym_core/src/weight_pipeline.*` | the pipeline of §4 (pure logic, unit-tested) |
| `lib/gym_core/src/weight_model.*` | the classifier of §5 (pure logic, unit-tested) |

### 6.1 Lifecycle

- **Boot:** after the LIS3DH and Bluefruit, the beacon looks for the magnetometer and loads its taught weights. Without one it prints `[weight] no magnetometer … weight sensing off`, then looks again every 30 s while no set is active.
- **First counted rep:** recording starts and the magnetometer wakes.
- **During the set:** a 50 Hz sampler reads the LIS3DH and the magnetometer into the buffer. It runs on battery too: its wake-up does not depend on USB, unlike the console.
- **Set timeout (5 s):** recording stops, the magnetometer sleeps, the pipeline runs, and the estimate is printed.
- **A set ended any other way** (`cal`, `reset`, `mode`, `axis`, a drift recalibration) is not weighed: `[weight] set aborted`.

In `lever` mode, weight sensing is off: the beacon must sit on the stack.

**Without a magnetometer and with the stream off, nothing here adds a timer, a wake-up or any I2C traffic.** Rep counting is then exactly as before. A magnetometer that stops answering mid-set (5 failed reads in a row) is dropped for that set with a message, and rep counting carries on.

### 6.2 Two changes to the rep counter's side

- **Advertising no longer blocks.** A rep used to block the loop for 150 ms while its radio burst went out. A rep is counted at the top of the lift, exactly where the recorder needs its samples. The burst now starts and returns, and `loop()` stops it 150 ms later. The packets and their timing on air are the same.
- **The LIS3DH runs in normal (10-bit) mode** instead of low-power (8-bit) mode: 4 mg steps instead of 16 mg. The dips the integration works with are only 40–150 mg. The interrupt thresholds still step in 16 mg, so a threshold can move by at most one step. The cost is about +5 µA idle (walkthrough §6).

### 6.3 The TMAG5273 register by register

| Register | Value | Meaning |
|---|---|---|
| `MANUFACTURER_ID` 0x0E/0x0F | read `49 54` | "TI": the chip is there |
| `DEVICE_ID` 0x0D | read, `& 0x03` | 1 = A1 (±40 mT), 2 = A2 (±133 mT) |
| `CONV_STATUS` 0x18 | `0x10` | clear the power-on flag |
| `DEVICE_CONFIG_1` 0x00 | `0x14` | 32x averaging, no magnet temperature compensation, no CRC |
| `SENSOR_CONFIG_1` 0x02 | `0x70` | X, Y and Z |
| `SENSOR_CONFIG_2` 0x03 | `0x00` | ±40 mT (the finest range) |
| `T_CONFIG` 0x07 | `0x01` | die temperature with every conversion |
| `INT_CONFIG_1` 0x08 | `0x01` | INT pin off (not wired; not on STEMMA QT) |
| `DEVICE_STATUS` 0x1C | `0x0F` | clear the latched error flags (a past supply dip would otherwise fail every conversion) |
| `DEVICE_CONFIG_2` 0x01 | `0x10` / `0x11` | low-noise standby / sleep, written last |

**Waking it:** while asleep the chip wakes on its address but does not acknowledge it. So `begin()` and `wake()` first send an address-only frame (the NACK is expected) and wait 100 µs before talking to it.

**Reading one sample:**
1. Write `[0x81, 0x10]`. Bit 7 of the register byte *triggers a conversion* (it is not an auto-increment flag as on ST parts).
2. Wait 3 ms with `delayMicroseconds()`, without touching the bus: a result that completes while the chip is addressed is dropped. The conversion takes 2.5 ms typically; the datasheet gives no maximum, hence the margin.
3. Read 9 bytes from 0x10: temperature, X, Y, Z, then the status.
4. The status's set counter must have moved (a fresh result). A power-on or diagnostic flag makes the driver set the chip up again.

The beacon repeats this 4 times per 50 Hz sample (about 13.4 ms) and averages. `BEACON_TMAG_OVERSAMPLE` allows 1–5; 6 would no longer fit in 20 ms.

### 6.4 RAM

| Buffer | Size |
|---|---|
| Set recorder: 60 s × 50 Hz of time, accelerometer, field, flags | 45.6 KB |
| Pipeline work arrays (2 × 3000 floats) | 24 KB |
| Motion analysis and rep features | 3.3 KB |
| Taught weights and their file buffer | 5.8 KB |
| **Beacon total static RAM** | **94.6 KB** of 243 KB (was 15.7 KB) |

All of it is static: the loop task's stack is only 4 KB. A set longer than `BEACON_W8_MAX_SET_S` (60 s) keeps its last 60 s, which still contain the rest after the last rep, and the console says so.

### 6.5 Power during a set

While a set is recorded:
- the TMAG5273 converts about 60 % of the time (≈1.8 mA on average);
- the CPU stays awake while it waits for the conversions.

Count roughly 3 mA during sets. At 30 sets of 40 s a day that is about 1 mAh a day, against 2.4 mAh a day of idle current. Between sets the TMAG5273 sleeps at 5 nA. The breakout's LED (§3.3) costs more than either.

---

## 7. Console commands

All rep-counting commands from walkthrough §7 still work. New:

| Command | Effect |
|---|---|
| `mag` | magnetometer, its state and temperature, the field at rest after the last set, whether the last set is ready to teach, the taught weights |
| `teach <w>` | label the last weighed set with weight `w`, one decimal allowed (`teach 50`, `teach 12.5`). Each set can be taught once |
| `weights` | list the taught weights and how many reps each has |
| `forget <w>` / `forget all` | delete a taught weight / all of them |
| `dump` | print the last set as CSV (§8) |
| `log` | toggle: dump every set automatically when it ends |
| `st` | the Teleplot stream now also shows `>bx >by >bz` (µT) when a magnetometer is fitted |

`s` shows one extra line, e.g. `weight: TMAG5273 at 0x35, 3 weights taught (42 reps)`.

**Units:** labels are plain numbers. Use the machine's unit; lb matches the UREC Live backend's `weightLbs`.

---

## 8. CSV dump

`dump` (or `log`) prints the last set so a random forest, or anything else, can be trained offline on exactly what the beacon saw. Lines starting with `#` describe the set; the rest is one row per sample:

```
# w8 set=143 reps=10 samples=1850 sensor=TMAG5273 dT_ms=2000 adapted=0
# error=none
# rest=1650..1725 gravity_mg=12.0,-8.0,1003.5 b_rest_0.1uT=1234,-567,890 temp_c=24.6
# counts_ms=0,2480,4950,...
# valleys=11,135,259,...
# segment=0 start=43 end=167 bottom=74 zenith=136 lift5=95 height_mm=297 valid=1
...
t_ms,ax_mg,ay_mg,az_mg,bx_0.1uT,by_0.1uT,bz_0.1uT,valid
0,16,-8,1004,1230,-560,880,1
...
# end
```

The dump is written a few lines at a time, only while the USB buffer has room, so a slow serial monitor never delays a rep. A new set aborts a dump in progress. Save it with the PlatformIO monitor's log filter or any terminal that logs to a file. For a labelled dataset, note the true weight of each set next to its `set=` number.

---

## 9. Teaching and testing at the gym (mirroring the paper's Table 6)

1. Mount the beacon on the top plate and wait for `cal`. Check that `s` shows the magnetometer.
2. **Teach** 3–4 weights you use, light and heavy. For each weight, do two sets: one full range, one about half range. Type `teach <w>` after each.
3. **Test:** 3 sets per weight, without teaching. Write down what the beacon said.

| Weight | Set | Truth | Beacon | Votes | Lift | Notes |
|---|---|---|---|---|---|---|
| 50 | 1 | 50 | | | | |
| 50 | 2 | 50 | | | | half range |
| 50 | 3 | 50 | | | | slow |

The paper reached 97.5 % on 6 weights with 10-fold cross-validation and 93.75 % on unseen users. Those numbers come from a 0.3 µT magnetometer and a random forest trained on 320 sets. Expect less from the TMAG5273 on heavy weights (§3.2).

---

## 10. Where this deviates from the paper, and why

| Topic | Paper | Here | Why |
|---|---|---|---|
| Magnetometer | BMM150 (DA14583 kit) | TMAG5273, then LIS2MDL | the hardware on hand; the LIS2MDL is the closer match |
| Where it runs | offline (Weka, R) | on the beacon, per set | no phone or PC in the loop |
| Classifier | random forest over the Table 3 features | 3-nearest-neighbour vote over 7 features, taught on the beacon | no training data yet; the CSV dump collects it |
| Segmentation | valleys < 60 %, ΔT = 2 s | the same, plus: a peak between valleys, the 60 % measured before the landing, the first dip of a pause kept, ΔT shortened when the rep counter saw faster reps, counted reps added when their dip was too shallow, merged reps dropped | pauses at the top, the landing bump, reps faster than 2 s, a slower first rep, uncounted half reps |
| Displacement | integrate, low-pass, integrate | per rep window (fastest lowering to fastest lowering), mean acceleration and mean velocity removed | the paper gives no filter cut-offs; this cancels bias and is exact when consecutive reps are alike (§4 step 5) |
| Magnetic features | statistics of the raw field per 2 s frame | change from the field at rest, at the top and 5 cm above the bottom | removes the sensor's offset and drift; addresses the height/weight ambiguity |
| Field at rest | not described | median of the first still 1.5 s after the last rep | |
| First rep | recorded | its lift is not | sampling all the time would drain the battery |
| Accelerometer resolution | – | LIS3DH normal mode, 4 mg | the integration needs it |

---

## 11. Tests

`test/test_gym_core/` holds 24 tests for the pipeline and the classifier, next to the rep-counting tests. The synthetic sets model a stack moving with sine or minimum-jerk strokes and the LIS3DH reading it (4 mg steps, 3 mg noise, the count interrupt that starts the recording). They cover:
- an upside-down board and a 40° tilt
- a fast lift with a slow lowering
- a constant bias
- a 2.5 s pause at the top, and pauses at the top or the bottom on some reps only
- a slower first rep, and half reps too shallow to be counted
- rep-to-rep variation of the range of motion (a bound on the known limit, §4 step 5)
- reps faster than ΔT
- a late start
- the landing ringing
- samples missing at every top
- a field offset that drifts between teaching and testing
- the height/weight ambiguity
- the classifier's corner cases (one weight taught, one example per weight, a three-way tie, zero spread, two reps)
- flash format round-trips and version checks

Run them with `pio test -e gateway` (walkthrough §11).

---

## 12. Troubleshooting

| Message / symptom | Likely cause / fix |
|---|---|
| `weight: no magnetometer` | STEMMA QT cable not seated, or the board is not at 0x35. Run an I2C scanner: you should see 0x18 and 0x35. |
| `not weighed: no still rest after the set` | Something moved during the first 2 s after the last rep: the lifter still holding the handle, the pin pulled at once, a wobbling mount. Let the stack rest for 2 s. |
| `not weighed: field not steady at rest` | Something magnetic moved near the sensor while the stack rested: a phone, a dumbbell, the selector pin. |
| `not weighed: no reps in the accelerometer signal` / `fewer than two rep tops found` | Very short or very gentle sets. Two reps are not enough. |
| `not weighed: no rep lifted the stack by 5 cm` | Partial reps only, or a mount that slips. |
| `uncertain - 4/8 reps say 50` | The weights are too close for the sensor (heavy end, §2.2), or not enough was taught. Teach more sets at both ranges. With the TMAG5273, consider the LIS2MDL. |
| `unknown - none of the reps looks like a taught weight` | That weight was never taught, or the beacon moved since teaching. Re-teach after moving it. |
| `teach not saved (flash)` | The Bluetooth stack did not start, so the beacon cannot write flash. The weights still work until the next reboot. |
| `magnetometer dropped out` / `stopped answering` | Loose STEMMA QT cable. A damaged cable can also hold the I2C bus low and freeze the whole beacon, rep counting included. Strain-relieve it. |
| Weight estimates drift over a session | The TMAG5273's offset drifts ±3 µT/°C. Per-set rest subtraction removes slow drift. If it persists, re-teach at the gym's temperature. |

---

## 13. Next steps

- **LIS2MDL** on the same STEMMA QT chain, classified side by side with the TMAG5273 (next commit on this branch).
- **Sending the weight on:** the iBeacon frame is full. The options are listed in walkthrough §14: an extra advertisement, BLE 5 extended advertising, or a scan response. The gateway would then add the weight to its set records and to Firebase.
- **Random forest:** collect CSV dumps with true weights, train with scikit-learn, export with emlearn.
