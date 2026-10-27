# Beacon Breadboard Layout: Complete Reference

This is a hand-off document for the breadboard prototype of a BLE gym-tracking **beacon**. Every hole position here was checked against Adafruit's own Fritzing parts and Fritzing's **half+** breadboard part, so parts placed on these holes in Fritzing line up exactly.

Companion guide (interactive): "Beacon Breadboard Layout" artifact, https://claude.ai/artifact/6QJfBw9XCj2JMSMctF4B85

---

## 1. What this circuit is

- **Beacon** = Adafruit Feather nRF52840 Express (BLE microcontroller) + one I2C sensor on a 400-point (half-size) breadboard, powered by a LiPo battery.
- The sensors all speak **I2C**: four lines (3.3 V, GND, SDA, SCL). They can share the same lines because each one has its own address.

| Sensor | Adafruit # | Role | I2C address |
|---|---|---|---|
| LIS3DH | 2809 | Accelerometer (motion / wake / reps) | 0x18 |
| LIS2MDL | 4488 | Magnetometer (weight sensing, approach 1) | 0x1E |
| TMAG5273 | 6489 | 3D Hall-effect magnetometer (weight sensing, approach 2) | 0x35 |

- **Only one sensor fits on the board** after the Feather. All three use the same "sensor slot" with VIN in F28, so you can swap them without moving any wires.
- **Normal setup:** the LIS3DH stays in the slot, and a magnetometer plugs into the LIS3DH's spare STEMMA QT socket with a cable (see §8). Both are then on the same I2C bus.
- Other parts of the wider system (not on this breadboard): a LilyGo T-Watch S3 (the user's watch, which detects proximity to the beacon) and an ESP32 gateway (scans for BLE and uploads over Wi-Fi).

### Parts on the breadboard
| Part | Adafruit # | Notes |
|---|---|---|
| Feather nRF52840 Express | 4062 | Micro-USB. Headers ship loose (see §9). |
| LIS3DH (STEMMA QT version) | 2809 | Two 6-pin rows 0.5 in apart, two STEMMA QT sockets |
| LIS2MDL | 4488 | One pin row, two STEMMA QT sockets |
| TMAG5273 | 6489 | One 6-pin row, two STEMMA QT sockets |
| 400 mAh LiPo | 3898 | Plugs into the Feather's JST battery socket. Not wired through the breadboard. |
| Half-size breadboard + jumper kit | 3314 | 400 points, 30 columns |

---

## 2. Breadboard coordinate system (Fritzing half+, 400 points)

- **Columns 1–30**, numbered left to right.
- **Rows A–J.** A is at the bottom and J at the top, as Fritzing draws it.
- **Terminal strips:** within one column, A–E are connected (bottom half) and F–J are connected (top half). A center channel separates E from F, so the two halves are not connected to each other.
- **Power rails:** four rails. Each runs the full length of the board as one continuous connection on the half+ part.

| Rail | Fritzing letter | Position | Polarity |
|---|---|---|---|
| Top outer | Z | above row J | **−** (GND) |
| Top inner | Y | just above row J | **+** (3.3 V) |
| Bottom inner | X | just below row A | **−** (GND) |
| Bottom outer | W | below X | **+** (3.3 V) |

- Fritzing names rail holes `pin<k><Rail>`, for example `pin4W`. Rail hole index *k* runs from 3 to 31, skipping 8, 14, 20 and 26 (the gaps between groups of five). Rail hole *k* sits about 0.037 in to the right of column *k−2*.
- **Real boards differ:** some physical boards stack the + and − rails the other way round. Always follow the printed red (+) and blue (−) lines on the real board.

Geometry (inches, from Fritzing's half+ SVG, useful for drawing or checking):
```
column n x  = 0.1515 + (n-1)*0.1
row y       = J .4835  I .5835  H .6835  G .7835  F .8835
              E 1.1835 D 1.2835 C 1.3835 B 1.4835 A 1.5835
rail y      = Z .0835  Y .1835  X 1.8835  W 1.9835
rail hole k = 0.1515 + 0.0372 + (k-3)*0.1
```

---

## 3. Feather nRF52840 placement

The Feather is unrotated, with the USB port at the left. It straddles the center channel, with its 16-pin row in row B and its 12-pin row in row H.

**16-pin side, row B (columns 1–16):**

| Col | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Pin | RST | **3V** | AREF | **GND** | A0 | A1 | A2 | A3 | A4 | A5 | SCK | MO | MI | RX | TX | D2 |

**12-pin side, row H (columns 5–16):**

| Col | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 | 15 | 16 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Pin | BAT | EN | USB | D13 | D12 | D11 | D10 | D9 | D6 | D5 | **SCL** | **SDA** |

Pins this build uses: **3V = B2**, **GND = B4**, **SCL = H15**, **SDA = H16**. D5 (H14) receives the LIS3DH's INT1 (wire 9, §8). D6 (H13) is still free for a second interrupt. RST in B1 is a handy check that the Feather is seated in the right place.

The Fritzing part uses different names for some pins: 3V → `3.3V`, RST → `RESET`, USB → `VBUS`, BAT → `VBAT`. The Fritzing part has **no battery (JST) connector**. The battery plugs into the real board's socket and is not drawn.

---

## 4. Sensor slot

Each sensor is **rotated 180°** (its labels read upside down) with **VIN in F28**. Its main pin row then runs leftward along row F.

| Hole | F28 | F27 | F26 | F25 | F24 | F23 | F22 |
|---|---|---|---|---|---|---|---|
| LIS3DH | VIN | 3Vo | GND | SCL | SDA | INT1 | – |
| LIS2MDL | VIN | 3Vo | GND | SCL | SDA | SDO | CS |
| TMAG5273 | VIN | 3Vo | GND | SCL | SDA | INT | – |

The **LIS3DH's second row** sits across the center channel in row C. The two rows are 0.5 in apart.

| Hole | C28 | C27 | C26 | C25 | C24 | C23 |
|---|---|---|---|---|---|---|
| LIS3DH | INT2 | A3 | A2 | A1 | SDO | CS |

Nothing in row C is wired. That row is there to hold the board flat, because a tilted accelerometer reads differently.

The Fritzing parts use different names for some pins: VIN → `VCC`, 3Vo → `3.3V`, A1–A3 → `ADC1`–`ADC3`, and the TMAG5273's INT → `!INT`.

Board footprint: each sensor is about 1.08 in long. Its body covers roughly columns 20–30 (it overhangs to the right of column 28), rows G to B, raised on its header pins.

---

## 5. Wire list (8 wires)

| # | Signal | From | To | Length | Purpose |
|---|---|---|---|---|---|
| 1 | 3V | A2 | bottom + rail (`pin4W`) | short | Feather 3V → bottom + rail |
| 2 | GND | A4 | bottom − rail (`pin6X`) | short | Feather GND → bottom − rail |
| 3 | 3V | bottom + rail (`pin21W`) | top + rail (`pin21Y`) | ≈1.8 in | Carries 3V to the top rails (the sensor is on the top half) |
| 4 | GND | bottom − rail (`pin22X`) | top − rail (`pin22Z`) | ≈1.8 in | Carries GND to the top rails |
| 5 | 3V | J28 | top + rail (`pin30Y`) | short | Sensor VIN |
| 6 | GND | J26 | top − rail (`pin28Z`) | short | Sensor GND |
| 7 | SCL | I15 | I25 | 1.0 in | Feather SCL → sensor SCL |
| 8 | SDA | J16 | J24 | 0.8 in | Feather SDA → sensor SDA |

Notes:
- Any hole on the correct rail works for a rail end; the hole names above are just what the layout uses.
- Wires 3 and 4 run in the gap right of the Feather. Wire 4's top end sits under the sensor's left end, which is fine because the sensor is raised on its pins. Bow a 2-inch kit wire slightly, or use a flexible jumper.
- Wires 7 and 8 cross over wires 3 and 4. Crossings don't matter for insulated jumpers.
- Wire colors in the guide show what each wire carries: red 3V, black GND, yellow SCL, blue SDA. Kit wires are colored by length instead.
- Wire 9 (LIS3DH INT1 → Feather D5) is listed in §8. Rep counting needs it.

### Resulting nets
- **3V:** Feather 3V (B2) → A2 → bottom + rail → wire 3 → top + rail → J28 → sensor VIN (F28)
- **GND:** Feather GND (B4) → A4 → bottom − rail → wire 4 → top − rail → J26 → sensor GND (F26)
- **SCL:** Feather SCL (H15) → I15 → wire 7 → I25 → sensor SCL (F25)
- **SDA:** Feather SDA (H16) → J16 → wire 8 → J24 → sensor SDA (F24)

### Free holes in the sensor columns (top half)
H28 (VIN), H26 (GND), H25 (SCL), H24 (SDA). H23 (INT/INT1) is now taken by wire 9 (§8). These are useful for extra connections, or for showing a STEMMA QT cable in Fritzing (§8).

---

## 6. Build order

1. **Solder the headers** (see §9). Push the strips into the breadboard with the long pins down, set the board on top and solder from above.
2. **Seat the Feather.** RST in B1, the 16-pin row in row B, and the 12-pin row in H5–H16.
3. **Power the bottom rails.** Wires 1 and 2.
4. **Bridge to the top rails.** Wires 3 and 4.
5. **Check the Feather.** Plug in USB, install Adafruit's nRF52 board package in the Arduino IDE and upload Blink. The red LED should blink. Then unplug USB.
6. **Seat the sensor.** Rotate it 180° and put VIN in F28. For the LIS3DH, the second row lands in C23–C28.
7. **Power the sensor.** Wires 5 and 6.
8. **Connect SCL and SDA.** Wires 7 and 8.
9. **Connect INT1.** Wire 9 runs H23 → I14 as a flexible jumper (see §8). Rep counting needs it.
10. **Scan for the sensor.** Run an I2C scanner sketch. It should report 0x18, 0x1E or 0x35. If it finds nothing, check that SCL and SDA aren't swapped and that VIN is in F28. To swap sensors, unplug USB and put the next board in the same holes.
11. **Connect the battery.** Plug the Adafruit 400 mAh LiPo into the JST socket next to the USB port. Use only Adafruit's battery, because some brands wire the plug with the opposite polarity. It charges whenever USB is plugged in.

### Checks before plugging in USB
- Red goes from A2 (3V) and black from A4 (GND). Reversed power can damage the sensor.
- At the sensor, J28 goes to the top + rail and J26 to the top − rail.
- Wire 7 runs I15 → I25 and wire 8 runs J16 → J24.
- Wire 9 runs H23 → I14 (LIS3DH INT1 → Feather D5, see §8).
- In columns 22–28 of the top half, only the sensor's pins and wires 5–9 are plugged in. Wire 9's end is in H23.
- Always unplug USB **and** the battery before moving any wire.

---

## 7. Fritzing setup

1. Drag in the breadboard from the Core parts bin and set its **size to `half+`** in the Inspector. Do not use the full-size part: the user has a 400-point board.
2. Import the four parts from Adafruit's Fritzing library (File → Open each one; they appear in the Temp bin):
   - https://github.com/adafruit/Fritzing-Library/blob/master/parts/Adafruit%20Feather%20nRF52840.fzpz
   - https://github.com/adafruit/Fritzing-Library/blob/master/parts/Adafruit%20LIS3DH%20Triple-Axis%20Accelerometer.fzpz
   - https://github.com/adafruit/Fritzing-Library/blob/master/parts/Adafruit%20LIS2MDL.fzpz
   - https://github.com/adafruit/Fritzing-Library/blob/master/parts/Adafruit%20TMAG5273%203D%20Hall%20Effect%20Magnetometer%20Breakout.fzpz
   - **Common mistakes:** using the nRF52**832** Feather part, or the old LIS3DH part (single 8-pin row). Use the parts above.
3. Drop the Feather **unrotated** with RESET on B1.
4. **Rotate each sensor 180° before placing it** so that VIN lands on F28. If the LIS3DH won't snap onto the holes, turn off View → Align to Grid, drag it until its pins turn green, then turn grid alignment back on. The part's STEMMA QT connection points sit off the 0.1 in grid, which can throw off snapping.
5. Draw the wires from §5. A connected wire end turns green; an unconnected end stays red.

---

## 8. Running the accelerometer and a magnetometer together

- Leave the LIS3DH in the slot. Plug the magnetometer into the LIS3DH's **right-hand STEMMA QT socket** with a STEMMA QT cable (100 mm is fine on the bench). That socket is wired internally to the LIS3DH's VIN, GND, SDA and SCL, so no extra breadboard wiring is needed. The magnetometer then needs **no headers**.
- A second magnetometer can plug into the first one's other STEMMA QT socket. The three addresses (0x18, 0x1E, 0x35) don't clash.
- STEMMA QT cable colors: red = V+, black = GND, blue = SDA, yellow = SCL.
- **In Fritzing** (there is no cable part): place the magnetometer beside the board and draw wires from its VIN, GND, SCL and SDA to **H28, H26, H25 and H24**. Electrically this is the same circuit as the cable.
- Keep the magnetometer a few centimeters away from the Feather, the battery and wires carrying current. They create small magnetic fields that disturb its readings.

### Interrupt wire 9 (required for rep counting)
The rep-counting firmware (`src/beacon/`, explained in `docs/rep-counting-walkthrough.md`) counts reps from the LIS3DH's INT1 interrupt, so this wire is now part of the build:

| # | Signal | From | To | Length | Purpose |
|---|---|---|---|---|---|
| 9 | INT1 | H23 | I14 | ≈0.9 in, flexible jumper | LIS3DH INT1 (F23) → Feather D5 (H14) |

- H23 is the free hole in the INT1 column. I14 is in the D5 column; I15 is wire 7's end, so I14 is the hole just left of it.
- Wires 7 and 8 take the straight path along rows I and J, so use a flexible jumper that arcs over them.
- The TMAG5273's INT also lands in F23. When the TMAG5273 sits in the slot instead of the LIS3DH, the same wire carries its interrupt.
- On a moving weight stack, tape or hot-glue the jumper ends so vibration can't pull them out. The firmware sets a pull-down on D5, so a loose wire reads LOW instead of firing random reps.

### Mounting on the gym machine (decided: the stack's top plate)
- **Never run the sensor cable from the moving weight stack to the fixed frame.** It would flex on every rep and could snag, and I2C isn't meant for long cables.
- The weight is read **during each lift**, following W8-Scope (`docs/weight-sensing.md`). The field at the top of the lift depends on how many plates were lifted. The selector pin can't move mid-set, so there is one estimate per set.
- **Option A (not used): everything on the frame.** The beacon and magnetometer would be fixed beside the stack with a short cable, and at rest the magnetometer would read the pin's position. Not used: a frame-mounted beacon cannot count reps (below), and W8-Scope reads the weight from the top plate.
- **Option B (chosen): everything on the stack's top plate.** The accelerometer gets a strong signal from every rep, and the magnetometer rides with the lifted plates, as in W8-Scope. Downsides: the impacts at the bottom of each rep can shake breadboard wires loose, and heavier settings change the field less (the paper's "U-shape").
- Planned test: zip-tie the finished breadboard beside the stack, do a few sets and log both sensors.
- **Rep counting (decided 2026-10-24):** the accelerometer counts reps only on a part that moves with every rep. The paper put its beacon on "the part of the machine that undergoes the most movement". This build mounts the beacon **on the moving weight stack** (firmware mode `stack`), or on a lever arm (mode `lever`).
  - The beacon must be fixed rigidly. The paper's one failure was a beacon that shifted: "a slight displacement caused the beacon to lose the orientation used for interrupt generation".
  - A beacon on the frame (Option A) does not feel enough motion to count reps.
- **Weight sensing (decided 2026-10-26):** the magnetometer rides on the top plate with the beacon, plugged into the LIS3DH's STEMMA QT socket. The TMAG5273 comes first; the LIS2MDL can daisy-chain behind it so the two can be compared on the same sets. `docs/weight-sensing.md` explains the method.
- **Power:** every Adafruit breakout has a green power LED (≈0.1–0.2 mA through 10 kΩ) and a regulator. That draws more than the whole beacon idles at (~100 µA). For battery use, remove the LED or its resistor on each board.

---

## 9. Soldering and headers (user status)

- The user has **no soldering experience**. They plan to practice first on scrap header strips and perfboard.
- **Feather:** needs both strips, 16 pins and 12. The user's Feather came with **only the 16-pin strip**. They still need 12 pins for the side with SCL and SDA. Options:
  - Use the magnetometers' header strips (each about 6 pins). These are free if the magnetometers connect by STEMMA QT cable.
  - Buy breakaway male headers. Micro Center sells Leo Sales "Break Away Headers, Straight, 10 pack", SKU 044826.
  - Break strips to length with needle-nose pliers at the notch between pins. Two pieces can sit end to end in one row.
- **LIS3DH:** solder both 6-pin rows.
- **Magnetometers:** no headers needed if they connect by STEMMA QT cable.
- **Later (optional):** a permanent version on an Adafruit Perma-Proto half-size PCB (#1609). It needs about 9 soldered wires in place of the jumpers, plus the Feather and LIS3DH (or sockets for them).

---

## 10. Quick facts for answering questions

- The breadboard is the **400-point half-size** board. The user briefly thought it was full-size, but it isn't. Ignore any advice about columns above 30.
- Pin order VIN, 3Vo, GND, SCL, SDA is the same on all three sensors. That's why they can swap into the same slot without rewiring.
- The rails on Fritzing's half+ breadboard run unbroken along the full length. On a real board, check whether the rail lines have a break in the middle.
- The Feather's 3V pin supplies 3.3 V to everything. Sensor VIN accepts 3.3 V.
- Source data for this layout is in the guide's HTML (constants `X1 = 0.1515`, `P = 0.1`, `ROWY`, `RAILY`, `WIRES`, `SENSORS`). A layout checker script confirmed that no pins share a strip, rail polarities are correct, no wire end sits under a board, and the wire lengths match the table.
