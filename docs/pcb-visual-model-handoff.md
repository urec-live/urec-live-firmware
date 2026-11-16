# Handoff: Visual Model of the Custom Beacon PCB

**For:** a fresh Claude Code session (Fable 5.2) in the workspace `c:\Users\abelv\portfolio\getmefit\arduino_code`. You have no memory of the session that wrote this, so everything you need is here or in the files it points to.
**Written:** 2026-11-12
**Repo:** `GetMeFit/` (git repo). Build everything in **`GetMeFit/hardware/beacon-pcb/`** (new folder).

---

## 1. The goal

The beacon prototype works on a breadboard: an Adafruit Feather nRF52840 Express, Adafruit sensor breakouts, and a LiPo battery. The next step toward mass production is a **custom PCB**. Your job is to produce a **visual model** of that custom board. It is a concept board (EVT-0 level), not a fab-ready design, and it has three parts:

1. A **code-defined board** in tscircuit, with real parts, real footprints, a real netlist, a board outline and a sensible placement.
2. **Exports** from it: a 3D model (GLB + STEP), PCB and schematic images, a KiCad project and a BOM.
3. An **interactive 3D viewer page**, published as a private Artifact, that lets the user orbit the board and see what each part is and why it sits where it does.

### Definition of done
- [ ] `tsci build` succeeds on the board with no errors.
- [ ] Every part in §4 is on the board with a footprint taken from a datasheet or an imported LCSC part, never invented.
- [ ] The netlist matches §5 (check it against the exported readable netlist).
- [ ] The placement passes the checklist in §6.
- [ ] The exports in §8 exist in `GetMeFit/hardware/beacon-pcb/out/`.
- [ ] The 3D viewer Artifact is published, and you have opened it and checked it renders.
- [ ] `GetMeFit/hardware/beacon-pcb/DESIGN-NOTES.md` exists, with the BOM, a decisions log, open questions and known gaps.
- [ ] Your final message follows §11.

Routing is **nice to have**. An unrouted board, with ratsnest lines and the reason routing was left out, is an acceptable result.

---

## 2. What the prototype is (facts; do not re-derive these)

Read these files only if you need more detail than this section gives:

| File | What it holds |
|---|---|
| `GetMeFit/beacon-breadboard-schematic.md` | prototype wiring, parts list, mounting decisions (§8) |
| `GetMeFit/docs/weight-sensing.md` | magnetometers, why their placement matters (§3), power (§6.5) |
| `GetMeFit/docs/rep-counting-walkthrough.md` | power budget (§6), battery reporting (§5.5) |
| `GetMeFit/src/beacon/beacon_config.h` | pins, I2C addresses, LED and console settings |
| `C:\Users\abelv\.platformio\packages\framework-arduinoadafruitnrf52\variants\feather_nrf52840_express\variant.cpp` | Feather pin → nRF52840 GPIO map |

**What the device does:**
- The beacon is mounted rigidly on the **top plate of a gym weight stack** and moves with every rep.
- An accelerometer counts reps, and a magnetometer estimates the weight lifted from how the steel plates bend the magnetic field.
- The beacon sends rep counts as BLE iBeacon advertisements to a separate ESP32-C3 gateway. **The gateway is out of scope for this task.**

**Prototype parts:**

| Function | Prototype part | Interface | Notes |
|---|---|---|---|
| MCU + BLE | nRF52840, on the Feather | – | the firmware uses **114.6 KB of static RAM**, so the custom board must stay on the nRF52840 (256 KB). The nRF52833 (128 KB) is too small. |
| Accelerometer | LIS3DH (Adafruit #2809) | I2C 0x18, INT1 → GPIO | register-level driver; keep this exact chip so the firmware doesn't change |
| Magnetometer A | LIS2MDL (Adafruit #4488) | I2C 0x1E | 0.3 µT noise; the closer match to the research paper's sensor |
| Magnetometer B | TMAG5273**A1** (Adafruit #6489) | I2C 0x35 | 3D Hall sensor, noisier; its INT pin is not used today |
| Data flash | GD25Q16C, 2 MB, QSPI | QSPI | stores every set's data (`set_store.*`) |
| Battery | 400 mAh 1S LiPo (Adafruit #3898), JST-PH | – | ~100 µA idle on the Feather, mostly from its regulator (55 µA) and VBAT divider (14 µA) |
| Charger | MCP73831, on the Feather | – | charges from USB |
| Regulator | AP2112K-3.3, on the Feather | – | 55 µA quiescent current: **replace it** |
| LEDs | red (rep feedback), blue | GPIO | the NeoPixel is not used |
| USB | Micro-USB, on the Feather | – | used for the console, flashing and the `download_sets.py` set download |

**Current GPIO assignment.** Keep these pins on the custom board, so the firmware ports unchanged:

| Signal | nRF52840 pin | Feather name |
|---|---|---|
| I2C SDA | P0.12 | SDA |
| I2C SCL | P0.11 | SCL |
| LIS3DH INT1 | P1.08 | D5 |
| spare interrupt | P0.07 | D6 (free; good for LIS3DH INT2 or TMAG5273 INT) |
| LED red | P1.15 | D3 / LED1 |
| LED blue | P1.10 | D4 / LED2 |
| Battery sense (AIN5) | P0.29 | A6 / VBAT |
| QSPI SCK / CS | P0.19 / P0.20 | – |
| QSPI IO0 / IO1 / IO2 / IO3 | P0.17 / P0.22 / P0.23 / P0.21 | – |
| 32.768 kHz crystal | P0.00 / P0.01 | – |
| nRESET | P0.18 | RST |
| User button (optional) | P1.02 | D7 |

---

## 3. Tool choice (already decided; don't re-litigate it)

**Use tscircuit** (code-first PCB design in TSX, CLI `tsci`), because:
- it runs on Node, which is installed (v22.20.0), and needs no GUI;
- it exports `glb`, `gltf`, `step`, `pcb-svg`, `pcb-png`, `schematic-svg`, `assembly-svg`, `kicad_zip`, `gerbers` and `readable-netlist` (`tsci export <file> -f <format>`);
- it has a live preview (`tsci dev`, at http://localhost:3020) and an official agent skill.

**Not installed:** `tsci`, KiCad and bun. **Don't install KiCad.** tscircuit covers the 3D output, and the `kicad_zip` export lets the user open the design in KiCad later.

**Fallback.** If tscircuit cannot do something essential after you have researched it (§9), say so in the notes. Don't switch tools silently. Known gap: `kicad_pcb` exports may not carry 3D model references, which is fine because the 3D deliverable comes from `glb`/`step`.

---

## 4. Concept BOM: defaults to build with

You're cleared to proceed on these defaults. Where a line says **research**, check it, choose, and record the choice in the decisions log. Prefer parts stocked at **LCSC / JLCPCB**, which makes footprints easy to import and keeps the board assemblable later.

| Ref | Part (default) | Package | Why / notes |
|---|---|---|---|
| U1 | **Raytac MDBT50Q-1MV2** (nRF52840, chip antenna, pre-certified FCC/IC/CE) | 10.5 × 15.5 × 2.05 mm LGA module, LCSC C5118826 | A pre-certified module saves most of the radio certification cost. **Research:** pad layout and antenna keep-out from Raytac's spec (Ver. L, pp. 12–13, "Keep out area"), and whether the module includes the 32.768 kHz crystal. |
| Y1 | 32.768 kHz crystal + load caps | 2012 or 3215 | Only if U1 doesn't include one. The firmware's BLE stack uses the LF crystal. |
| U2 | LIS3DH | LGA-16, 3 × 3 × 1 mm | SA0/SDO → GND (0x18), CS → VDD (I2C mode) |
| U3 | LIS2MDL | LGA-12, 2 × 2 × 0.7 mm | primary magnetometer |
| U4 | TMAG5273A1 | SOT-23-6 | Alternate magnetometer, marked **DNP** (do not populate) in the BOM. EVT boards keep both so the two can be compared. |
| U5 | GD25Q16C (or an equivalent 2 MB QSPI NOR) | USON-8 / WSON-8 | Keep it for now, because the set store needs it. Flag it as a **cost-down candidate** in the notes. |
| U6 | MCP73831T-2 LiPo charger | SOT-23-5 | Same part as the Feather. Choose the PROG resistor for ~100–200 mA. |
| U7 | Low-Iq 3.3 V LDO, e.g. **TI TPS7A0233** (25 nA Iq) | SOT-23-5 or X2SON | **Research:** compare it with powering the nRF52840 from VDDH directly (its internal REG0 can supply 3.3 V to the sensors). Pick one and log why. |
| J1 | USB-C receptacle, USB 2.0, 16-pin | SMD/through-hole hybrid | Charging, console, DFU and set download. Needs 5.1 kΩ CC pull-downs. |
| J2 | JST-PH 2-pin (or two solder pads for a pouch cell) | – | Battery. Mark + and − clearly; JST polarity varies between battery vendors. |
| J3 | **Tag-Connect TC2030-NL** footprint | no-leg, pads only | SWD (VDD, SWDIO, SWDCLK, nRESET, GND) for factory programming |
| D1, D2 | Red and blue LEDs | 0603 | ~1–2.2 kΩ series resistors (low current is fine) |
| SW1 | Tactile reset switch | small SMD | Reset also reopens the 10-minute BLE console window. |
| R/C | Battery divider to P0.29 | 0402 | **Research:** the Feather's 1:2 divider draws 14 µA. Use higher values plus a cap, or a load switch, and note the firmware change (acquisition time, divider ratio). |
| R/C | I2C pull-ups (4.7 kΩ), decoupling per datasheets, USB ESD protection (e.g. a USBLC6-2) | 0402 / SOT-23-6 | – |
| H1, H2 | Mounting holes for M2 screws | – | Use non-magnetic screws: nylon, brass or 316 stainless (see §6). |
| BT1 | 400 mAh 1S LiPo pouch, **3D placeholder only** | – | Look up Adafruit #3898's dimensions. It sits under the board on the bottom side. |

Optional: if it's easy, route LIS3DH INT2 or TMAG5273 INT to P0.07. A fuel gauge is **not** needed.

---

## 5. Netlist summary

- **VBUS** (J1) → charger U6 VIN, U1 VBUS (USB detect), ESD part.
- **VBAT** (J2+) ↔ U6 VBAT → LDO U7 IN (or U1 VDDH, if you take that route) → battery divider → P0.29.
- **3V3** → U1 VDD, U2, U3, U4, U5, I2C pull-ups, J3 VDD, LED anodes (or cathodes, depending on drive polarity).
- **GND** everywhere. Plan for a solid ground plane.
- **USB D+/D−**: J1 → ESD → U1 D+/D−. Keep the pair short and matched.
- **I2C**: SDA P0.12 and SCL P0.11 → U2, U3, U4.
- **INT**: U2 INT1 → P1.08.
- **QSPI**: U5 ↔ U1 on the pins in §2. Keep the traces short.
- **LEDs**: P1.15 → red, P1.10 → blue.
- **Reset**: SW1 and J3 nRESET → P0.18.
- **SWD**: J3 → U1 SWDIO and SWDCLK.

When something doesn't fit this summary (for example a module pin name you don't recognise), the module datasheet is authoritative.

---

## 6. Placement rules: self-check before exporting

1. **Antenna:** U1's antenna end sits at a board edge, with the full keep-out from Raytac's spec on **every layer**: no copper, no pour, no parts. The battery placeholder must not sit under the antenna.
2. **Magnetometers (most important for this product):** the weight estimate is a field change of only a few µT. Put U3 and U4 at the **far end of the board** from:
   - the USB-C shell (steel),
   - the battery and its leads,
   - the charger and LDO current paths,
   - any inductor (check whether U1 has internal DC/DC inductors).
   Aim for the largest distance the outline allows; the prototype kept "a few centimetres". Keep ferromagnetic parts (nickel-plated connectors, steel screws, shields) away from them.
3. **Accelerometer:** near the mounting holes or the board's centre, so it moves rigidly with the stack. Its axes should align with the board edges. Note its orientation in the notes.
4. **Battery:** stacked under the board. The USB-C and J2 sit at the same end, away from the magnetometers.
5. **Size target:** about 50 × 25 mm or smaller, a slim rectangle that fits a top-plate mount. If the rules above force it larger, that's acceptable; say why in the notes.
6. **Decoupling caps** go next to their pins. **QSPI flash** goes close to U1.
7. **J3 (Tag-Connect)** and test pads (VBAT, 3V3, GND, SDA, SCL) on the **bottom** side, reachable through a test fixture.
8. **Layers:** try 4 layers (signal / GND / power / signal). If tscircuit's autorouter handles only 2 well, use 2 and note that production should be 4 layers.

Render the board (`pcb-png` / `pcb-svg`, and a 3D snapshot if available) and **look at the images with the Read tool** to check them against this list. Don't rely on reading the code alone.

---

## 7. Work plan

Work in phases. After each phase, write a one-line status to the user, e.g. "Phase 2 done: schematic exports cleanly; now placing parts".

**Phase 0: Set up tools (time-box: 20 min)**
1. Install the CLI. Check the current command in the tscircuit docs; it's likely `npm install -g tscircuit`, which provides `tsci`.
2. Install the agent skill: `npx skills add tscircuit/skill`. Read its `SKILL.md`, `SYNTAX.md`, `WORKFLOW.md` and `CHECKLIST.md`.
3. Run `tsci init` in `GetMeFit/hardware/beacon-pcb/`. Add a `.gitignore` there with `node_modules/` (and any build caches).
4. Smoke test: build the template and export a `glb`. This proves the export path works before you invest in the design.

**Phase 1: Parts and footprints**
- Fill in §4 with exact part numbers and LCSC numbers.
- Import footprints from LCSC/JLCPCB where tscircuit supports it (check the docs and skill for the import command). Otherwise build them from the datasheet's land pattern.
- Check each footprint's pad count and pitch against the datasheet.

**Phase 2: Schematic / netlist**
- Write the TSX for §5. Use clear net names (`VBAT`, `V3V3`, `SDA`, …).
- Export `schematic-svg` and `readable-netlist`, and check them against §2 and §5.

**Phase 3: Outline and placement**
- Apply §6, render, look at the result, adjust, and repeat until the checklist passes.

**Phase 4: Routing (optional; time-box: 45 min)**
- Try the autorouter. If it fails or makes a mess, disable routing and record that.

**Phase 5: Exports**
- See §8.

**Phase 6: 3D viewer Artifact**
- See §10.

**Phase 7: Notes and final report**
- See §11.

---

## 8. Exports (into `GetMeFit/hardware/beacon-pcb/out/`)

`beacon.glb`, `beacon.step`, `pcb-top.svg`, `pcb-bottom.svg` (`--layer bottom`), `schematic.svg`, `assembly.svg`, `netlist.txt` (readable netlist), `beacon-kicad.zip`, and `bom.csv`. Generate the BOM CSV yourself if tscircuit doesn't export one: refdes, part, package, LCSC number, DNP flag, purpose.

Don't export gerbers for fabrication. This is a concept board, and gerbers invite someone to order it.

---

## 9. When you're unsure: research protocol

The user wants you to **research rather than guess, and rather than stop to ask**, whenever you're unsure what to do. In this order:

1. **This handoff and the repo docs in §2** cover the product facts.
2. **tscircuit syntax and CLI behaviour:**
   - the installed skill;
   - `https://docs.tscircuit.com/llms.txt`, which is about 1 MB. Download it to your scratchpad and Grep it rather than reading it whole;
   - `tsci <command> --help`;
   - GitHub issues on `tscircuit/*`.
3. **Part data:** manufacturer datasheet PDF > LCSC/JLCPCB listing > SnapMagic or Ultra Librarian. **Never invent pad geometry or pinouts.** Library footprints for U1 exist (Flux and SnapMagic list KiCad ones), but check them against Raytac's pad drawing.
4. **RF and layout:** Raytac's MDBT50Q spec and footprint design guide, Nordic's nRF52840 product specification, and Nordic DevZone hardware guidelines.
5. **Searching:** use WebSearch `standard` mode first; use `extended` only when standard results are thin or out of date. Give one question about **10 minutes**. If it's still unresolved, pick the conservative option, log it as an open question in `DESIGN-NOTES.md`, and move on.

**Stop and ask the user only for:**
- anything that spends money or creates an account;
- anything outward-facing: ordering boards, `tsci login` / `tsci push` (push **publishes to the public tscircuit registry**, so never run it), git commit or push, or making anything public;
- installing large system software (for example KiCad).

Everything else is your call. Log it.

---

## 10. The 3D viewer Artifact

- **Load the Artifact tool's design guidance first.** Run the quickstart the tool describes, with intent `other`.
- **The page:** a single interactive page that loads `beacon.glb` as a published supporting file, rendered with three.js from cdnjs at a pinned version.
- **It should show:**
  - orbit, zoom and pan, with a reset-view button;
  - a click or hover callout for each major part: refdes, part, its role in the beacon, and why it sits where it does;
  - toggles for:
    - top/bottom view or flip;
    - an outline of the antenna keep-out;
    - the magnetometer "quiet zone", i.e. the distance to the USB-C shell and the battery;
    - the battery placeholder;
  - a dimension readout of the board size;
  - a small BOM table under the viewer;
  - a clear **"Concept model, not for fabrication"** label.
- **Publishing:** publish privately (the default) and give the user the link. If the GLB is too large or the loader fails, fall back to pre-rendered PNG views in the same layout, and say so.

---

## 11. Final report (your last message)

Keep it short:
1. **Link** to the Artifact.
2. **What exists:** the folder and its key files.
3. **Decisions made:** one line each. The full log is in `DESIGN-NOTES.md`.
4. **Open questions for the user:** list these whether or not you hit them:
   - **USB-C vs. a sealed design** with pogo or charging pads (sweat and chalk exposure in a gym);
   - **LIS2MDL vs. TMAG5273**, to be decided by the gym-test results;
   - **keep or drop the 2 MB flash** in production;
   - **coin cell vs. LiPo**, once the idle current has been measured on the custom board;
   - the **gateway form factor**, which is out of scope here.
5. **Known gaps:** e.g. unrouted, unverified footprints, no DRC, no RF tuning.

Don't modify the firmware (`src/`, `lib/`, `test/`). Don't commit. Don't put the user's name or email in any external request.
