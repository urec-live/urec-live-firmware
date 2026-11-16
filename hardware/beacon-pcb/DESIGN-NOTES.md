# GetMeFit beacon PCB: design notes

**Status:** concept board (EVT-0), built 2026-11-12/13. **Not for fabrication.** No gerbers are exported, on purpose.

**What it is:** the breadboard beacon (Feather nRF52840 Express, Adafruit sensor breakouts, LiPo) collapsed onto one 50 × 25 mm, 4-layer board. Every pin matches the Feather, so the firmware ports unchanged.

**3D viewer:** https://claude.ai/artifact/CdyBoEs6Y2jcS2Jbppganf (private to the owner's account).

Contents: [Files](#files) · [Rebuild](#rebuild) · [Board summary](#board-summary) · [BOM](#bom) · [Pins and nets](#pins-and-nets) · [Placement checklist](#placement-checklist) · [Decisions log](#decisions-log) · [Routing](#routing) · [Firmware impact](#firmware-impact) · [Open questions](#open-questions) · [Known gaps](#known-gaps) · [Sources](#sources)

---

## Files

| Path | What it is |
|---|---|
| `index.circuit.tsx` | tsci entry point. Renders `Beacon` with routing and plane pours on. |
| `Beacon.tsx` | The board: every part, net and placement. |
| `layout.ts` | Geometry constants: outline, module position, antenna keep-out, battery, holes, connectors. |
| `bom.ts` | BOM, one row per reference designator, with the viewer's callout texts. |
| `imports/` | Parts imported from LCSC with `tsci import` (footprint, pin names, 3D model link). |
| `parts/TagConnectTC2030NL.tsx` | Tag-Connect footprint, redrawn from the KiCad library footprint. |
| `models/` | Placeholder 3D models for the module and battery (made by `scripts/make-models.ts`). |
| `scripts/` | `export-all.ts`, `make-bom.ts`, `make-viewer-data.ts`, `check-routing.ts`, `build-viewer.ts`, `serve-viewer.ts`, `shot.ps1`. |
| `tools/` | Inspection scripts used to check footprints and 3D positions (`dump-pads.ts`, `glb-bounds.ts`, …). |
| `viewer/` | The 3D viewer page (`template.html` → `index.html`) and its fallback images. |
| `out/` | The exports (below). |

### Exports in `out/`

| File | Size | Notes |
|---|---|---|
| `beacon.glb` | 7.5 MB | 3D model. Every part is a named node (`U1`, `J2`, …). |
| `beacon.step` | 19.5 MB | 3D model for mechanical CAD. Includes the module and battery placeholders. |
| `pcb-top.svg`, `pcb-bottom.svg` | 1.0 MB, 0.3 MB | Copper, pours, silkscreen, keep-out. |
| `schematic.svg` | 0.5 MB | Auto-placed schematic (see [Known gaps](#known-gaps)). |
| `assembly.svg` | 0.1 MB | Assembly drawing. |
| `netlist.txt` | 12 KB | Readable netlist. Checked against the handoff. |
| `beacon-kicad.zip` | 24 MB | KiCad project: `circuit.kicad_pcb`, `circuit.kicad_sch`, 3D models. Not opened in KiCad. |
| `bom.csv` | 4 KB | 51 rows: refdes, part, mpn, package, lcsc, dnp, purpose. |
| `viewer-data.json` | 20 KB | Callouts, zones and measured clearances for the viewer. |

## Rebuild

Node 22 is enough. The tscircuit CLI needs the bun runtime, which is installed inside this folder (`node_modules`), not system-wide.

```
npm install          # once
npm run build        # tsci build: must end "1 passed", exit code 0
npm run export       # builds, then writes everything in out/ (about 50 s)
npm run viewer       # assembles viewer/index.html and viewer/beacon.gltf.json from out/
npm run serve        # http://127.0.0.1:4173/_local.html for a local look
npm run footprints   # prints every pad of every imported part, for checking against datasheets
```

`npm run export` also cross-checks the BOM against the board and fails if any trace, via or pour is inside the antenna keep-out.

Never run `tsci push`: it publishes to the public tscircuit registry.

## Board summary

| | |
|---|---|
| Outline | 50 × 25 mm, 1.5 mm corner radius, 1.6 mm thick |
| Layers | 4: top signal + GND fill / inner1 GND / inner2 3V3 / bottom signal + GND fill |
| Overall height | 14.9 mm: battery 7.8 + board 1.6 + J2 (tallest top part) 5.5 |
| Top side | everything except the items below |
| Bottom side | Tag-Connect pads (J3), five test pads (TP1–TP5), the battery |
| Sensor end (−X) | magnetometers U3 and U4, and nothing magnetic |
| Power end (+X) | USB-C, battery connector, charger, LDO |
| Antenna edge (+Y, centre) | module U1, antenna end 0.3 mm inside the board edge |

Coordinates used below and in `layout.ts`: origin at the board centre, +X right, +Y up, seen from the top.

## BOM

One row per distinct part. `out/bom.csv` has one row per reference designator.

| Ref | Part | MPN | Package | LCSC | Fit | Purpose |
|---|---|---|---|---|---|---|
| U1 | nRF52840 BLE module, chip antenna | Raytac MDBT50Q-1MV2 | LGA-61, 10.5 × 15.5 × 2.05 mm | C5118826 | yes | MCU and radio |
| Y1 | 32.768 kHz crystal, 9 pF | SF32WK32768D91T004 | 3215 | C5374762 | yes | Low-frequency clock for BLE |
| U2 | 3-axis accelerometer | ST LIS3DHTR | LGA-16, 3 × 3 mm | C15134 | yes | Rep counting, I2C 0x18 |
| U3 | 3-axis magnetometer | ST LIS2MDLTR | LGA-12, 2 × 2 mm | C919695 | yes | Primary weight sensor, I2C 0x1E |
| U4 | 3-axis Hall sensor | TI TMAG5273A1QDBVR | SOT-23-6 | C3716049 | **DNP** | Alternate weight sensor, I2C 0x35 |
| U5 | 2 MB QSPI NOR flash | GigaDevice GD25Q16CEIGR | USON-8, 3 × 2 mm | C2973793 | yes | Set store. **Cost-down candidate** |
| U6 | LiPo charger, 4.2 V | Microchip MCP73831T-2ACI/OT | SOT-23-5 | C424093 | yes | 100 mA charge from USB |
| U7 | 3.3 V LDO, 25 nA Iq | TI TPS7A0233PDBVR | SOT-23-5 | C2887324 | yes | 3.3 V rail |
| U8 | USB ESD protection | ST USBLC6-2SC6 | SOT-23-6 | C7519 | yes | D+, D−, VBUS |
| J1 | USB-C receptacle, 16-pin | tscircuit `usb_c` standard | SMD + shell stakes | C393939 | yes | Charging, console, DFU, set download |
| J2 | JST-PH 2-pin, side entry | JST S2B-PH-SM4-TB | SMD | C295747 | yes | Battery. Pin 1 = + |
| J3 | Tag-Connect TC2030-NL | footprint only | 6 pads, 3 holes | – | no part | SWD programming |
| D1 | Red LED | KT-0603R | 0603 | C2286 | yes | Rep feedback, P1.15 |
| D2 | Blue LED | Everlight 19-217/BHC-ZL1M2RY/3T | 0603 | C72041 | yes | BLE/console status, P1.10 |
| SW1 | Tactile switch | Alps SKRPACE010 | 4.2 × 3.2 mm | C139797 | yes | Reset |
| C1, C2 | 12 pF C0G | – | 0402 | C1547 | yes | Y1 load capacitors |
| C3, C4, C8 | 4.7 µF X5R 16 V | – | 0603 | C19666 | yes | Charger in, charger out, module VBUS |
| C5, C6 | 1 µF X5R 25 V | – | 0402 | C52923 | yes | LDO in and out |
| C7, C12 | 10 µF X5R 10 V | – | 0603 | C19702 | yes | Module VDD bulk, LIS3DH bulk |
| C9, C10, C11, C13, C16 | 100 nF X7R 16 V | – | 0402 | C1525 | yes | Decoupling; C16 is the battery-sense reservoir |
| C14 | 220 nF X7R 16 V | – | 0402 | C16772 | yes | LIS2MDL C1 pin |
| C15 | 100 nF X7R 16 V | – | 0402 | C1525 | **DNP** | TMAG5273 decoupling (fit with U4) |
| R1 | 2.2 kΩ | – | 0402 | C25879 | yes | Red LED, about 0.6 mA |
| R2 | 1 kΩ | – | 0402 | C11702 | yes | Blue LED, about 0.4 mA |
| R3, R4 | 5.1 kΩ | – | 0402 | C25905 | yes | USB-C CC pull-downs |
| R5, R6 | 27 Ω | – | 0402 | C25100 | yes | USB series resistors |
| R7 | 10 kΩ | – | 0402 | C25744 | yes | Charger PROG: 100 mA |
| R8, R9 | 1 MΩ | – | 0402 | C26083 | yes | Battery divider |
| R10, R11 | 4.7 kΩ | – | 0402 | C25900 | yes | I2C pull-ups |
| R12 | 10 kΩ | – | 0402 | C25744 | **DNP** | TMAG5273 INT pull-up (fit with U4) |
| TP1–TP5 | Test pads, 1.5 mm | – | bottom | – | no part | VBAT, 3V3, GND, SDA, SCL |
| H1, H2 | M2 mounting holes, 2.2 mm | – | – | – | no part | Non-magnetic screws only |
| BT1 | 400 mAh LiPo pouch | LP801735 (Adafruit #3898) | about 36 × 17 × 7.8 mm | – | no part | 3D placeholder; plugs into J2 |

51 reference designators, 39 parts fitted. All LCSC numbers were looked up, not recalled: the ICs by importing them, the passives against the JLCPCB catalogue.

## Pins and nets

`out/netlist.txt` was checked net by net against the handoff's §2 pin table and §5 netlist. It matches.

| Net | Module pin | Goes to |
|---|---|---|
| SDA / SCL | P0.12 / P0.11 | U2, U3, U4, R11 / R10, TP4 / TP5 |
| ACC_INT1 | P1.08 | U2 INT1 |
| ACC_INT2 | P0.07 | U2 INT2 (the optional spare interrupt) |
| LED_RED / LED_BLUE | P1.15 / P1.10 | R1 → D1, R2 → D2, cathodes to GND (active high, as on the Feather) |
| VBAT_SENSE | P0.29 (AIN5) | R8/R9 divider, C16 |
| QSPI SCK / CS | P0.19 / P0.20 | U5 |
| QSPI IO0 / IO1 / IO2 / IO3 | P0.17 / P0.22 / P0.23 / P0.21 | U5 |
| XL1 / XL2 | P0.00 / P0.01 | Y1, C1 / C2 |
| NRESET | P0.18 | SW1, J3 pin 3 |
| SWDIO / SWDCLK | SWDIO / SWDCLK | J3 pins 2 / 4 |
| USB_DP / USB_DM | D+ / D− through R6 / R5 | U8, J1 |
| VBUS | VBUS | J1, U6 VDD, U8, C3, C8, C9 |
| VBAT | – | J2 pin 1, U6 VBAT, U7 IN and EN, R8, C4, C5, TP1 |
| V3V3 | VDD and VDDH | U7 OUT and every 3.3 V load, J3 pin 1, TP2 |

Left unconnected on purpose: U1 DCCH (REG0 unused), U6 STAT, U3 INT/DRDY, U2 ADC1–3, J1 SBU1/SBU2, J3 SWO, and the user button pin P1.02.

## Placement checklist

The handoff's §6, checked on the rendered images and by measurement (`out/viewer-data.json`, `scripts/check-routing.ts`).

| # | Rule | Result |
|---|---|---|
| 1 | Antenna at a board edge, full keep-out on every layer, battery not under it | **Pass.** Module antenna end 0.3 mm inside the +Y edge. Keep-out 12.4 × 4.1 mm (Raytac's 12.4 × 3.8 plus the 0.3 mm to the edge) plus the 1.6 × 1.2 mm top-layer window. No trace, via, pour or part in it on any of the 4 layers. Battery stops 2.9 mm short of it. |
| 2 | Magnetometers at the far end from USB-C, battery, charger/LDO, inductors | **Pass, with one weak point.** See the table below. The battery *body* ends 10.5 mm away. |
| 3 | Accelerometer near a mounting hole, axes aligned | **Pass.** U2 is 6.5 mm from H2, unrotated. |
| 4 | Battery under the board; USB-C and J2 at the same end | **Pass.** |
| 5 | About 50 × 25 mm | **Pass.** Exactly 50 × 25 mm. |
| 6 | Decoupling at the pins; flash close to U1 | **Pass.** Flash is about 2 mm from the module's QSPI pads. |
| 7 | J3 and test pads on the bottom, reachable | **Pass.** All outside the battery's shadow. |
| 8 | 4 layers | **Pass.** |

Shortest distance from the nearer magnetometer:

| To | Distance |
|---|---|
| USB-C shell (steel) | 39.0 mm |
| Battery tabs, protection board and leads | 44.0 mm |
| Battery connector J2 | 37.1 mm |
| Charger U6 | 31.5 mm |
| LDO U7 | 31.5 mm |
| Module shield can (DC/DC inductor inside) | 18.6 mm |
| Battery cell body | 10.5 mm |
| Screw head at mounting hole H2 | 6.5 mm |

## Decisions log

1. **Module: Raytac MDBT50Q-1MV2.** The handoff's default, and the module the Feather itself carries. The LCSC footprint was checked pad by pad against Raytac's "recommended solder pad layout" (spec Ver. K, p. 12): 61 pads of 0.6 × 0.4 mm, every coordinate matches.
2. **32.768 kHz crystal added (Y1).** Raytac's spec says the module contains the 32 MHz crystal and the REG1 DC/DC inductor, but not the low-frequency crystal. A 9 pF crystal with 12 pF load capacitors follows Raytac's reference circuit.
3. **Module inset 0.3 mm from the board edge.** Keeps its courtyard on the board. The keep-out is extended by the same 0.3 mm.
4. **Power: TPS7A0233 LDO, module in normal-voltage mode (VDD = VDDH).** This is Raytac's reference circuit 8.3. The alternative, the battery straight into VDDH with the chip's REG0 making 3.3 V, was rejected because:
   - REG0's output defaults to 1.8 V until the UICR is programmed, and the flash needs 2.7 V;
   - the bootloader and firmware would need changes, and the handoff asks for none;
   - Nordic limits what REG0 may supply to external parts (about 25 mA running; not re-checked in this session), which is tight with flash writes.
   No DC/DC inductor is added, so the only inductor on the board is the one inside the module.
5. **Battery divider: 1 MΩ / 1 MΩ with a 100 nF reservoir.** Same 1:2 ratio as the Feather, one tenth of the current (2.1 µA at 4.2 V). A load switch would save that too but needs a GPIO and firmware.
6. **Charge current 100 mA** (10 kΩ PROG), as on the Feather: 0.25 C for the 400 mAh cell.
7. **USB series resistors (27 Ω) and VBUS capacitors.** The resistors follow Raytac's USB reference circuit. Raytac shows 10 µF on the module's VBUS pin; 4.7 µF is used there and 4.7 µF at the charger, to stay inside USB's 10 µF inrush limit.
8. **TMAG5273 INT pulled up, not grounded.** TI allows grounding an unused INT only with a register setting (`MASK_INTB`). A 10 kΩ pull-up needs no firmware change.
9. **LIS3DH INT2 → P0.07.** The optional spare interrupt, since it was free to route.
10. **J2 faces the bottom edge**, at the power end. Facing the short edge beside the USB-C would not fit in 25 mm.
11. **Reset switch wired on diagonal pins,** so the net list is right whichever way its internal pairs run.
12. **Mounting holes on a diagonal,** both outside the battery's shadow, H2 beside the accelerometer.
13. **Tag-Connect footprint redrawn in TSX** from the KiCad library geometry, only to drop KiCad's fabrication-layer text. Pinout: TC2030-CTX (1 VTref, 2 SWDIO, 3 nRESET, 4 SWCLK, 5 GND, 6 SWO).
14. **Passives kept unrotated.** tscircuit does not rotate rectangular courtyards with the part, so rotated passives raise false overlaps and can hide real ones.
15. **J2's 3D model moved 2.825 mm** (`imports/S2B_PH_SM4_TB_LF__SN_.tsx`). As imported, the housing sat behind its pads. Checked in the GLB afterwards.
16. **Tooling.** bun is a dev dependency of this folder rather than a system install. The viewer loads the model as glTF JSON because the Artifact host does not serve `.glb` files; it is the same content as `out/beacon.glb`.

## Routing

Routing is **on**, and it is for illustration only.

- tscircuit's autorouter completed all nets on 4 layers: 135 traces, 92 vias, 734 mm of track. The result is the same on every build.
- tscircuit reports no errors and no shorts. Nothing is inside the antenna keep-out.
- Pours: GND on top, inner1 and bottom; 3V3 on inner2.

What it is not:

- Nobody has reviewed it.
- Every trace is 0.15 mm wide, including VBUS, VBAT and 3V3.
- Vias are 0.3 mm pad / 0.2 mm drill, smaller than most fabs' standard.
- The USB pair is not length-matched or impedance-controlled.
- Several signal and supply traces cross the sensor end.

A production layout should be done by hand: sensor end kept free of supply traces, unbroken ground under the module, proper power widths.

To see placement and ratsnest lines only, render `<Beacon />` with its defaults (routing off, no pours).

## Firmware impact

None required. Pins, I2C addresses, LED polarity and the battery divider ratio are unchanged.

- `src/beacon/battery.cpp` works as it is. Its comments about a "~50 kOhm source" and "~14 uA" go stale: the source is now 500 kΩ behind a 100 nF reservoir, drawing about 2 µA. Eight back-to-back samples sag the reservoir by about 0.02 %.
- Wait 250 ms after power-up before the first battery reading (the reservoir charges through 500 kΩ).
- If U4 is ever fitted with INT grounded instead of pulled up, set `MASK_INTB`.

## Open questions

For the product:

1. **USB-C or a sealed design** with pogo or charging pads? A gym means sweat and chalk.
2. **LIS2MDL or TMAG5273?** To be decided by the gym-test results. The board carries both footprints.
3. **Keep or drop the 2 MB flash** in production?
4. **Coin cell or LiPo?** Decide once the idle current has been measured on a custom board.
5. **Gateway form factor.** Out of scope here.

Raised by this design:

6. **Battery body 10.5 mm from the magnetometers.** The tabs, leads, charger and USB shell are 31–44 mm away, but the cell itself is close. If the gym test shows the cell disturbs the field, lengthen the board by about 10 mm at the sensor end, or use a shorter cell.
7. **JST polarity.** J2 pin 1 is + and the silkscreen says so, but which side that is on a given battery was not verified. Check before first power-up.
8. **Raytac spec revision.** The handoff cites Ver. L; Ver. K was the newest that could be downloaded. Re-check the keep-out and pad drawing against Ver. L.
9. **Module shield can and inductor.** Is the can magnetic? Today's firmware never enables the module's DC/DC converter (the Adafruit core has no call that does). Enabling it would cut radio current but put a switching inductor 18.6 mm from the sensors.
10. **No power path and no reverse-polarity protection.** The system runs from the charger output in parallel with the cell. Fine at this load; decide whether production needs more.
11. **No charge indicator.** The charger's STAT pin is unconnected.
12. **Enclosure and mounting.** The hole positions, the 14.9 mm height and the J2 exit direction all depend on it.
13. **Capacitors beside the magnetometers.** MLCC terminations contain nickel. C13 and C14 are 2 mm from U3. Usual practice, but worth a look in the gym data.

## Known gaps

- **Routing:** autorouted and unreviewed (see [Routing](#routing)). No fab design rules were applied; the only checks are tscircuit's own.
- **Footprints not checked against the manufacturer's drawing.** Checked against the manufacturer: U1 (pad drawing and keep-out) and U4 (pin functions). The rest come from LCSC/EasyEDA and were checked for pin count, pitch and pin order only: U2, U3, U5, U6, U7, U8, J2, Y1, SW1, D1, D2. ST's site was unreachable, so the LIS3DH and LIS2MDL land patterns in particular are unverified. J1 uses tscircuit's built-in USB-C footprint.
- **Generic passives.** The 0402 and 0603 footprints are tscircuit's own, not LCSC's (the build warns about the difference).
- **Accelerometer axis signs** not confirmed against ST's drawing. The axes are parallel to the board edges; the firmware calibrates gravity itself.
- **Schematic:** auto-placed on one sheet, with no sections. Electrically right, untidy to read.
- **3D:** the module and battery are plain boxes. J3 and the test pads have no 3D shape. Battery size is from retail listings, which differ by up to 2 mm.
- **KiCad project and STEP file** were exported but not opened in KiCad or a CAD tool.
- **No RF work:** no tuning, and the antenna's behaviour above a steel plate with a battery under the board is unknown. The keep-out is Raytac's minimum.
- **Viewer:** checked locally in headless Edge, in both themes, including a click test on all 18 annotated parts. The hosted page itself was not opened from this session. If the 3D model fails to load there, the page shows three pre-rendered views instead.
- **Process:** `npx skills add tscircuit/skill` was blocked by the session's permission rules. `tsci init` then installed the same skill files into `.claude/skills/` and `.agents/skills/` in this folder as a side effect. Delete those two folders if they are not wanted.

## Sources

- Raytac MDBT50Q-1MV2 / P1MV2 spec, Ver. K: pad layout (p. 12), keep-out (pp. 9–14), pin assignment (pp. 16–19), reference circuits (ch. 8). https://cdn.sparkfun.com/assets/4/7/4/3/8/_nRF52840__MDBT50Q-1MV2___MDBT50Q-P1MV2_Ver.K_spec.pdf
- TI TMAG5273 datasheet: pin functions, INT handling. https://www.ti.com/lit/ds/symlink/tmag5273.pdf
- LCSC / JLCPCB listings for every part number in the BOM.
- KiCad library footprint `Connector:Tag-Connect_TC2030-IDC-NL_2x03_P1.27mm_Vertical`.
- tscircuit 0.0.3168 docs (`https://docs.tscircuit.com/llms.txt`) and its bundled skill.
- Repo: `beacon-breadboard-schematic.md`, `docs/weight-sensing.md`, `src/beacon/battery.cpp`.
