// GetMeFit beacon: custom PCB concept (EVT-0). NOT FOR FABRICATION.
// Entry point for tsci: index.circuit.tsx, which renders this component.
//
// What this is: the breadboard beacon (Feather nRF52840 Express + Adafruit sensor breakouts)
// collapsed onto one 50 x 25 mm board. Pins match the Feather so the firmware ports unchanged.
// Design rationale, BOM and open questions: DESIGN-NOTES.md. Geometry constants: layout.ts.
//
// Board frame: origin at the board centre, +X right, +Y up (top view).
//   -X end = sensor end (magnetometers)      +X end = power end (USB-C, battery, charger, LDO)
//   +Y edge, centre = antenna edge
import { MDBT50Q_1MV2 } from "./imports/MDBT50Q_1MV2"
import { LIS3DHTR } from "./imports/LIS3DHTR"
import { LIS2MDLTR } from "./imports/LIS2MDLTR"
import { TMAG5273A1QDBVR } from "./imports/TMAG5273A1QDBVR"
import { GD25Q16CEIGR } from "./imports/GD25Q16CEIGR"
import { MCP73831T_2ACI_OT } from "./imports/MCP73831T_2ACI_OT"
import { TPS7A0233PDBVR } from "./imports/TPS7A0233PDBVR"
import { USBLC6_2SC6 } from "./imports/USBLC6_2SC6"
import { S2B_PH_SM4_TB_LF__SN_ } from "./imports/S2B_PH_SM4_TB_LF__SN_"
import { SF32WK32768D91T004 } from "./imports/SF32WK32768D91T004"
import { SKRPACE010 } from "./imports/SKRPACE010"
import { KT_0603R } from "./imports/KT_0603R"
import { A_19_217_BHC_ZL1M2RY_3T } from "./imports/A_19_217_BHC_ZL1M2RY_3T"
import { TagConnectTC2030NL } from "./parts/TagConnectTC2030NL"
import moduleModelUrl from "./models/mdbt50q.glb"
import batteryModelUrl from "./models/lipo-400mah.glb"
import { lcsc } from "./bom"
import { SECTIONS, sch } from "./schematic-layout"
import {
  ANTENNA_KEEPOUT,
  ANTENNA_TOP_WINDOW,
  BATTERY,
  BOARD,
  HOLES,
  HOLE_DIAMETER,
  JST,
  MAG,
  MODULE,
  RULES,
  TAG,
  TEST_PAD_ROW,
  USB,
} from "./layout"

// Net names. Every connection below goes through one of these.
const N = {
  GND: "net.GND",
  VBUS: "net.VBUS", // USB 5 V
  VBAT: "net.VBAT", // LiPo +, 3.0 to 4.2 V
  V3V3: "net.V3V3", // LDO output
  USB_DP: "net.USB_DP", // connector side of the series resistors
  USB_DM: "net.USB_DM",
  USB_DP_MCU: "net.USB_DP_MCU",
  USB_DM_MCU: "net.USB_DM_MCU",
  CC1: "net.CC1",
  CC2: "net.CC2",
  SDA: "net.SDA", // P0.12
  SCL: "net.SCL", // P0.11
  ACC_INT1: "net.ACC_INT1", // LIS3DH INT1 -> P1.08
  ACC_INT2: "net.ACC_INT2", // LIS3DH INT2 -> P0.07 (spare interrupt)
  TMAG_INT: "net.TMAG_INT", // pulled up; not routed to the MCU (unused today)
  QSPI_SCK: "net.QSPI_SCK", // P0.19
  QSPI_CS: "net.QSPI_CS", // P0.20
  QSPI_IO0: "net.QSPI_IO0", // P0.17
  QSPI_IO1: "net.QSPI_IO1", // P0.22
  QSPI_IO2: "net.QSPI_IO2", // P0.23
  QSPI_IO3: "net.QSPI_IO3", // P0.21
  LED_RED: "net.LED_RED", // P1.15, active high
  LED_BLUE: "net.LED_BLUE", // P1.10, active high
  LED_RED_A: "net.LED_RED_A",
  LED_BLUE_A: "net.LED_BLUE_A",
  VBAT_SENSE: "net.VBAT_SENSE", // P0.29 / AIN5, VBAT / 2
  NRESET: "net.NRESET", // P0.18
  SWDIO: "net.SWDIO",
  SWDCLK: "net.SWDCLK",
  XL1: "net.XL1", // P0.00
  XL2: "net.XL2", // P0.01
  CHG_PROG: "net.CHG_PROG",
  MAG_C1: "net.MAG_C1",
} as const

/** Plane fills stop this far inside the board edge (a little more than the edge rule). */
const POUR_EDGE_MARGIN = `${RULES.edgeClearance + 0.1}mm`

/**
 * The beacon board. `routingDisabled` leaves the nets as ratsnest lines (the default for the
 * concept model); pass `false` to let tscircuit's autorouter try.
 */
export const Beacon = ({
  routingDisabled = true,
  pours = false,
}: {
  routingDisabled?: boolean
  /** Add the plane fills: GND on top, inner1 and bottom, 3V3 on inner2. */
  pours?: boolean
}) => (
  <board
    width={`${BOARD.width}mm`}
    height={`${BOARD.height}mm`}
    thickness={`${BOARD.thickness}mm`}
    layers={BOARD.layers}
    borderRadius="1.5mm"
    title="GetMeFit beacon concept board"
    routingDisabled={routingDisabled}
    // Design rules: inside JLCPCB's published 4-layer limits (0.09 mm track/space, 0.15 mm via
    // hole, 0.25 mm via pad, 0.2 mm copper to edge) with margin, so the autorouter cannot
    // produce something no board house would make.
    minTraceWidth={RULES.traceWidth}
    nominalTraceWidth={RULES.traceWidth}
    minViaHoleDiameter={RULES.viaHole}
    minViaPadDiameter={RULES.viaPad}
    pcbStyle={{ viaHoleDiameter: RULES.viaHole, viaPadDiameter: RULES.viaPad }}
    minBoardEdgeClearance={RULES.edgeClearance}
    // Schematic: connect by net label rather than by long wires across the sheet.
    schTraceAutoLabelEnabled
    schMaxTraceDistance={3}
  >
    {/* ------------------------------------------------------------------ Supply nets */}
    {/* Wider tracks where current flows. The planes do most of the work for GND and 3V3. */}
    <net name="GND" isGroundNet nominalTraceWidth={RULES.powerTraceWidth} />
    <net name="V3V3" isPowerNet nominalTraceWidth={RULES.powerTraceWidth} />
    <net name="VBUS" isPowerNet nominalTraceWidth={RULES.chargeTraceWidth} />
    <net name="VBAT" isPowerNet nominalTraceWidth={RULES.chargeTraceWidth} />

    {/* Schematic blocks; each part's position on the sheet is in schematic-layout.ts. */}
    {SECTIONS.map((s) => (
      <schematicsection key={s.name} name={s.name} displayName={s.title} />
    ))}

    {/* ------------------------------------------------------------------ MCU + radio */}
    {/* U1 sits with its antenna end flush on the +Y edge. Normal-voltage mode (VDD = VDDH),
        REG0 unused, DCCH open: the same supply arrangement as the Feather. */}
    <MDBT50Q_1MV2
      name="U1" {...sch("U1")}
      pcbX={MODULE.x}
      pcbY={MODULE.y}
      // The model's own origin is the footprint origin (centre of the pad field), not the
      // centre of its bounding box: the antenna end overhangs the pads by 3.5 mm.
      cadModel={<cadmodel modelUrl={moduleModelUrl} modelOriginPosition={{ x: 0, y: 0, z: 0 }} />}
      connections={{
        GND1: N.GND,
        GND2: N.GND,
        GND3: N.GND,
        GND4: N.GND,
        GND5: N.GND,
        VDD: N.V3V3,
        VDDH: N.V3V3,
        VBUS: N.VBUS,
        D_POS: N.USB_DP_MCU,
        D_NEG: N.USB_DM_MCU,
        P0_12: N.SDA,
        P0_11: N.SCL,
        P1_08: N.ACC_INT1,
        P0_07: N.ACC_INT2,
        P1_15: N.LED_RED,
        P1_10: N.LED_BLUE,
        P0_29: N.VBAT_SENSE,
        P0_19: N.QSPI_SCK,
        P0_20: N.QSPI_CS,
        P0_17: N.QSPI_IO0,
        P0_22: N.QSPI_IO1,
        P0_23: N.QSPI_IO2,
        P0_21: N.QSPI_IO3,
        P0_00: N.XL1,
        P0_01: N.XL2,
        P0_18: N.NRESET,
        SWDIO: N.SWDIO,
        SWDCLK: N.SWDCLK,
      }}
    />
    {/* Antenna keep-out: the whole strip along the antenna edge, no copper, pour or parts on
        any layer. Raytac's minimum is 12.4 x 3.8 mm; see layout.ts. */}
    <keepout
      shape="rect"
      pcbX={ANTENNA_KEEPOUT.x}
      pcbY={ANTENNA_KEEPOUT.y}
      width={`${ANTENNA_KEEPOUT.width}mm`}
      height={`${ANTENNA_KEEPOUT.depth}mm`}
      layers={["top", "inner1", "inner2", "bottom"]}
      excludeRefs={[".U1"]}
    />
    {/* Raytac "Top layer no ground pad" window, top copper only. */}
    <keepout
      shape="rect"
      pcbX={ANTENNA_TOP_WINDOW.x}
      pcbY={ANTENNA_TOP_WINDOW.y}
      width={`${ANTENNA_TOP_WINDOW.width}mm`}
      height={`${ANTENNA_TOP_WINDOW.height}mm`}
      layers={["top"]}
      excludeRefs={[".U1"]}
    />
    <capacitor name="C7" {...sch("C7")} {...lcsc("C7")} capacitance="10uF" footprint="0603" pcbX={4.45} pcbY={-4.6} connections={{ pin1: N.V3V3, pin2: N.GND }} />
    <capacitor name="C8" {...sch("C8")} {...lcsc("C8")} capacitance="4.7uF" footprint="0603" pcbX={7.55} pcbY={-4.6} connections={{ pin1: N.VBUS, pin2: N.GND }} />

    {/* 32.768 kHz crystal: not inside the module (Raytac spec, ch. 8). 9 pF crystal with
        12 pF load capacitors, as in Raytac's reference circuit. */}
    <SF32WK32768D91T004
      name="Y1" {...sch("Y1")}
      loadCapacitance="9pF"
      // Left pad straight below the module's XL1 pad, so both crystal tracks stay on the top
      // layer with no vias (tscircuit checks crystal nets for that, and for length <= 10 mm).
      pcbX={-0.2}
      pcbY={-5.0}
      connections={{ pin1: N.XL1, pin2: N.XL2 }}
    />
    {/* Load capacitors stand upright on either side of the crystal, a pad's width away. */}
    <capacitor name="C1" {...sch("C1")} {...lcsc("C1")} capacitance="12pF" footprint="0402" pcbX={-2.78} pcbY={-5.0} pcbRotation={90} connections={{ pin1: N.GND, pin2: N.XL1 }} />
    <capacitor name="C2" {...sch("C2")} {...lcsc("C2")} capacitance="12pF" footprint="0402" pcbX={2.38} pcbY={-5.0} pcbRotation={90} connections={{ pin1: N.GND, pin2: N.XL2 }} />

    {/* Reset. The switch's internal pairs are 1-2 and 3-4; diagonal pins are used so the net
        list is right whichever way the pairs run. */}
    <SKRPACE010 name="SW1" {...sch("SW1")} pcbX={3.0} pcbY={-10.3} connections={{ pin1: N.NRESET, pin4: N.GND }} />

    {/* ------------------------------------------------------------------ Data flash */}
    <GD25Q16CEIGR
      name="U5" {...sch("U5")}
      pcbX={9.6}
      pcbY={3.2}
      connections={{
        VCC: N.V3V3,
        VSS: N.GND,
        EP: N.GND,
        N_CS: N.QSPI_CS,
        SCLK: N.QSPI_SCK,
        IO0: N.QSPI_IO0,
        IO1: N.QSPI_IO1,
        IO2: N.QSPI_IO2,
        IO3: N.QSPI_IO3,
      }}
    />
    <capacitor name="C10" {...sch("C10")} {...lcsc("C10")} capacitance="100nF" footprint="0402" pcbX={9.6} pcbY={6.0} connections={{ pin1: N.V3V3, pin2: N.GND }} />

    {/* ------------------------------------------------------------------ USB-C */}
    <connector
      name="J1" {...sch("J1")}
      standard="usb_c"
      pcbX={USB.x}
      pcbY={USB.y}
      pcbRotation={USB.rotation}
      connections={{
        VBUS1: N.VBUS,
        VBUS2: N.VBUS,
        GND1: N.GND,
        GND2: N.GND,
        SHELL1: N.GND,
        SHELL2: N.GND,
        SHELL3: N.GND,
        SHELL4: N.GND,
        CC1: N.CC1,
        CC2: N.CC2,
        DP1: N.USB_DP,
        DP2: N.USB_DP,
        DM1: N.USB_DM,
        DM2: N.USB_DM,
      }}
    />
    {/* 5.1 k pull-downs mark the port as a USB device (sink). */}
    <resistor name="R3" {...sch("R3")} {...lcsc("R3")} resistance="5.1k" footprint="0402" pcbX={15.5} pcbY={0.1} connections={{ pin1: N.CC1, pin2: N.GND }} />
    <resistor name="R4" {...sch("R4")} {...lcsc("R4")} resistance="5.1k" footprint="0402" pcbX={15.5} pcbY={5.4} connections={{ pin1: N.CC2, pin2: N.GND }} />
    {/* USBLC6-2SC6: pins 1/6 = I/O1, 3/4 = I/O2, 2 = GND, 5 = VBUS */}
    <USBLC6_2SC6
      name="U8" {...sch("U8")}
      pcbX={13.9}
      pcbY={2.7}
      connections={{
        pin1: N.USB_DM,
        pin6: N.USB_DM,
        pin3: N.USB_DP,
        pin4: N.USB_DP,
        pin2: N.GND,
        pin5: N.VBUS,
      }}
    />
    <capacitor name="C9" {...sch("C9")} {...lcsc("C9")} capacitance="100nF" footprint="0402" pcbX={13.2} pcbY={5.4} connections={{ pin1: N.VBUS, pin2: N.GND }} />
    {/* 27 ohm series resistors, per Raytac's USB reference circuit. */}
    <resistor name="R5" {...sch("R5")} {...lcsc("R5")} resistance="27" footprint="0402" pcbX={8.8} pcbY={-1.6} connections={{ pin1: N.USB_DM_MCU, pin2: N.USB_DM }} />
    <resistor name="R6" {...sch("R6")} {...lcsc("R6")} resistance="27" footprint="0402" pcbX={8.8} pcbY={-0.5} connections={{ pin1: N.USB_DP_MCU, pin2: N.USB_DP }} />

    {/* ------------------------------------------------------------------ Battery, charger, LDO */}
    {/* J2 pin 1 = battery +, pin 2 = battery -. Check against the cell before first power-up:
        JST-PH polarity is not standardised between battery vendors. */}
    <S2B_PH_SM4_TB_LF__SN_
      name="J2" {...sch("J2")}
      pcbX={JST.x}
      pcbY={JST.y}
      pcbRotation={JST.rotation}
      connections={{ pin1: N.VBAT, pin2: N.GND, pin3: N.GND, pin4: N.GND }}
    />
    <silkscreentext text="+" pcbX={JST.x - 2.5} pcbY={JST.y + 0.6} fontSize="1.2mm" />
    <silkscreentext text="-" pcbX={JST.x + 2.5} pcbY={JST.y + 0.6} fontSize="1.2mm" />
    <MCP73831T_2ACI_OT
      name="U6" {...sch("U6")}
      pcbX={11.2}
      pcbY={-5.0}
      connections={{ VDD: N.VBUS, VSS: N.GND, VBAT: N.VBAT, PROG: N.CHG_PROG }}
    />
    {/* I_charge = 1000 V / R_prog = 100 mA (0.25 C for a 400 mAh cell), as on the Feather. */}
    <resistor name="R7" {...sch("R7")} {...lcsc("R7")} resistance="10k" footprint="0402" pcbX={8.5} pcbY={-3.0} connections={{ pin1: N.CHG_PROG, pin2: N.GND }} />
    <capacitor name="C3" {...sch("C3")} {...lcsc("C3")} capacitance="4.7uF" footprint="0603" pcbX={13.5} pcbY={-1.6} connections={{ pin1: N.VBUS, pin2: N.GND }} />
    <capacitor name="C4" {...sch("C4")} {...lcsc("C4")} capacitance="4.7uF" footprint="0603" pcbX={11.6} pcbY={-8.0} connections={{ pin1: N.VBAT, pin2: N.GND }} />
    {/* TPS7A0233: 25 nA quiescent current, replaces the Feather's AP2112K (55 uA). */}
    <TPS7A0233PDBVR
      name="U7" {...sch("U7")}
      pcbX={11.2}
      pcbY={-10.5}
      connections={{ IN: N.VBAT, EN: N.VBAT, GND: N.GND, OUT: N.V3V3 }}
    />
    <capacitor name="C5" {...sch("C5")} {...lcsc("C5")} capacitance="1uF" footprint="0402" pcbX={14.2} pcbY={-11.0} connections={{ pin1: N.VBAT, pin2: N.GND }} />
    <capacitor name="C6" {...sch("C6")} {...lcsc("C6")} capacitance="1uF" footprint="0402" pcbX={8.1} pcbY={-11.0} connections={{ pin1: N.V3V3, pin2: N.GND }} />

    {/* Battery sense: 1 M / 1 M divider (same 1:2 ratio as the Feather, 2.1 uA instead of 21 uA
        at 4.2 V) with a 100 nF reservoir so the SAADC sees a low source impedance. */}
    <resistor name="R8" {...sch("R8")} {...lcsc("R8")} resistance="1M" footprint="0402" pcbX={-5.0} pcbY={0.9} connections={{ pin1: N.VBAT, pin2: N.VBAT_SENSE }} />
    <resistor name="R9" {...sch("R9")} {...lcsc("R9")} resistance="1M" footprint="0402" pcbX={-5.0} pcbY={2.0} connections={{ pin1: N.VBAT_SENSE, pin2: N.GND }} />
    <capacitor name="C16" {...sch("C16")} {...lcsc("C16")} capacitance="100nF" footprint="0402" pcbX={-5.0} pcbY={-0.2} connections={{ pin1: N.VBAT_SENSE, pin2: N.GND }} />

    {/* ------------------------------------------------------------------ LEDs (active high, as on the Feather) */}
    <resistor name="R1" {...sch("R1")} {...lcsc("R1")} resistance="2.2k" footprint="0402" pcbX={-5.0} pcbY={3.1} connections={{ pin1: N.LED_RED, pin2: N.LED_RED_A }} />
    <KT_0603R name="D1" {...sch("D1")} pcbX={-7.6} pcbY={3.1} connections={{ anode: N.LED_RED_A, cathode: N.GND }} />
    <resistor name="R2" {...sch("R2")} {...lcsc("R2")} resistance="1k" footprint="0402" pcbX={-5.0} pcbY={6.3} connections={{ pin1: N.LED_BLUE, pin2: N.LED_BLUE_A }} />
    <A_19_217_BHC_ZL1M2RY_3T name="D2" {...sch("D2")} pcbX={-7.6} pcbY={6.3} connections={{ anode: N.LED_BLUE_A, cathode: N.GND }} />

    {/* ------------------------------------------------------------------ I2C bus */}
    <resistor name="R10" {...sch("R10")} {...lcsc("R10")} resistance="4.7k" footprint="0402" pcbX={3.05} pcbY={-6.8} connections={{ pin1: N.V3V3, pin2: N.SCL }} />
    <resistor name="R11" {...sch("R11")} {...lcsc("R11")} resistance="4.7k" footprint="0402" pcbX={4.95} pcbY={-6.8} connections={{ pin1: N.V3V3, pin2: N.SDA }} />

    {/* ------------------------------------------------------------------ Accelerometer */}
    {/* LIS3DH at 0x18 (SA0 low), I2C mode (CS high). Package edges are parallel to the board
        edges, so its X/Y axes line up with the board's; Z is normal to the board. */}
    <LIS3DHTR
      name="U2" {...sch("U2")}
      pcbX={-9.0}
      pcbY={-6.5}
      connections={{
        VDD: N.V3V3,
        VDD_IO: N.V3V3,
        GND1: N.GND,
        GND2: N.GND,
        RES: N.GND,
        CS: N.V3V3,
        SA0: N.GND,
        SCL: N.SCL,
        SDA: N.SDA,
        INT1: N.ACC_INT1,
        INT2: N.ACC_INT2,
      }}
    />
    {/* Top layer kept clear under the LGA package, as for U3. */}
    <keepout shape="rect" pcbX={-9.0} pcbY={-6.5} width="1.7mm" height="1.7mm" layers={["top"]} excludeRefs={[".U2"]} />
    <capacitor name="C11" {...sch("C11")} {...lcsc("C11")} capacitance="100nF" footprint="0402" pcbX={-9.0} pcbY={-3.6} connections={{ pin1: N.V3V3, pin2: N.GND }} />
    <capacitor name="C12" {...sch("C12")} {...lcsc("C12")} capacitance="10uF" footprint="0603" pcbX={-9.0} pcbY={-9.5} connections={{ pin1: N.V3V3, pin2: N.GND }} />

    {/* ------------------------------------------------------------------ Magnetometers (sensor end) */}
    {/* U3 LIS2MDL at 0x1E: the primary weight sensor. Turned 180 degrees so pins 5 (C1) and
        6 (GND) face the board edge, where C14 sits right against them: ST's datasheet says
        C1 "must be connected as close as possible to pins 5 and 6" because of the high
        current pulses it carries. */}
    <LIS2MDLTR
      name="U3" {...sch("U3")}
      pcbX={MAG.U3.x}
      pcbY={MAG.U3.y}
      pcbRotation={180}
      connections={{
        Vdd: N.V3V3,
        Vdd_IO: N.V3V3,
        GND1: N.GND,
        GND2: N.GND,
        CS: N.V3V3,
        SCL: N.SCL,
        SDA: N.SDA,
        C1: N.MAG_C1,
      }}
    />
    {/* No tracks, vias or pour on the top layer under the package (ST's LGA mounting guidance:
        copper under a MEMS/AMR sensor adds solder stress and, here, stray current next to the die). */}
    <keepout shape="rect" pcbX={MAG.U3.x} pcbY={MAG.U3.y} width="1.0mm" height="1.0mm" layers={["top"]} excludeRefs={[".U3"]} />
    {/* C14 stands upright beside pins 5/6: top pad to C1, bottom pad to GND. */}
    <capacitor name="C14" {...sch("C14")} {...lcsc("C14")} capacitance="220nF" footprint="0402" pcbX={-23.25} pcbY={MAG.U3.y} pcbRotation={90} connections={{ pin1: N.GND, pin2: N.MAG_C1 }} />
    {/* C13 (100 nF) under pins 8/9 (GND, Vdd), C17 (10 uF) next to it: ST asks for both at pin 9. */}
    <capacitor name="C13" {...sch("C13")} {...lcsc("C13")} capacitance="100nF" footprint="0402" pcbX={MAG.U3.x} pcbY={2.2} connections={{ pin1: N.GND, pin2: N.V3V3 }} />
    <capacitor name="C17" {...sch("C17")} {...lcsc("C17")} capacitance="10uF" footprint="0603" pcbX={-18.7} pcbY={5.6} connections={{ pin1: N.V3V3, pin2: N.GND }} />
    {/* U4 TMAG5273A1 at 0x35: the alternate, not populated by default (EVT comparison only). */}
    <TMAG5273A1QDBVR
      name="U4" {...sch("U4")}
      pcbX={MAG.U4.x}
      pcbY={MAG.U4.y}
      doNotPlace
      connections={{
        VCC: N.V3V3,
        GND: N.GND,
        GND_TEST: N.GND,
        SCL: N.SCL,
        SDA: N.SDA,
        N_INT: N.TMAG_INT,
      }}
    />
    {/* Nothing on the top layer under the Hall sensor either: a via there would put current right under the die. */}
    <keepout shape="rect" pcbX={MAG.U4.x} pcbY={MAG.U4.y} width="1.1mm" height="2.6mm" layers={["top"]} excludeRefs={[".U4"]} />
    <capacitor name="C15" {...sch("C15")} {...lcsc("C15")} capacitance="100nF" footprint="0402" pcbX={-21.5} pcbY={-6.4} doNotPlace connections={{ pin1: N.V3V3, pin2: N.GND }} />
    <resistor name="R12" {...sch("R12")} {...lcsc("R12")} resistance="10k" footprint="0402" pcbX={-21.5} pcbY={-1.6} doNotPlace connections={{ pin1: N.V3V3, pin2: N.TMAG_INT }} />

    {/* ------------------------------------------------------------------ Programming and test (bottom side) */}
    {/* Tag-Connect TC2030-NL, TC2030-CTX pinout: 1 VTref, 2 SWDIO, 3 nRESET, 4 SWCLK, 5 GND, 6 SWO. */}
    <TagConnectTC2030NL
      name="J3" {...sch("J3")}
      layer="bottom"
      pcbX={TAG.x}
      pcbY={TAG.y}
      connections={{ VTREF: N.V3V3, SWDIO: N.SWDIO, NRESET: N.NRESET, SWDCLK: N.SWDCLK, GND: N.GND }}
    />
    {/* Tag-Connect's drawing forbids tracks and vias between the two pad rows. */}
    <keepout shape="rect" pcbX={TAG.x} pcbY={TAG.y} width="3.2mm" height="0.45mm" layers={["bottom"]} excludeRefs={[".J3"]} />
    {/* Test pads, bottom side, 2.5 mm apart. VBAT is the one farthest from the magnetometers. */}
    <testpoint name="TP1" {...sch("TP1")} footprintVariant="pad" padShape="circle" padDiameter={`${TEST_PAD_ROW.diameter}mm`} layer="bottom" pcbX={-13.5} pcbY={TEST_PAD_ROW.y} connections={{ pin1: N.VBAT }} />
    <testpoint name="TP2" {...sch("TP2")} footprintVariant="pad" padShape="circle" padDiameter={`${TEST_PAD_ROW.diameter}mm`} layer="bottom" pcbX={-16} pcbY={TEST_PAD_ROW.y} connections={{ pin1: N.V3V3 }} />
    <testpoint name="TP3" {...sch("TP3")} footprintVariant="pad" padShape="circle" padDiameter={`${TEST_PAD_ROW.diameter}mm`} layer="bottom" pcbX={-18.5} pcbY={TEST_PAD_ROW.y} connections={{ pin1: N.GND }} />
    <testpoint name="TP4" {...sch("TP4")} footprintVariant="pad" padShape="circle" padDiameter={`${TEST_PAD_ROW.diameter}mm`} layer="bottom" pcbX={-21} pcbY={TEST_PAD_ROW.y} connections={{ pin1: N.SDA }} />
    <testpoint name="TP5" {...sch("TP5")} footprintVariant="pad" padShape="circle" padDiameter={`${TEST_PAD_ROW.diameter}mm`} layer="bottom" pcbX={-23.5} pcbY={TEST_PAD_ROW.y} connections={{ pin1: N.SCL }} />

    {/* ------------------------------------------------------------------ Mechanical */}
    {/* M2 clearance holes. Use nylon, brass or 316 stainless screws: ordinary steel screws
        would disturb the magnetometers. */}
    <hole name="H1" diameter={`${HOLE_DIAMETER}mm`} pcbX={HOLES.H1.x} pcbY={HOLES.H1.y} />
    <hole name="H2" diameter={`${HOLE_DIAMETER}mm`} pcbX={HOLES.H2.x} pcbY={HOLES.H2.y} />

    {/* BT1: 400 mAh LiPo pouch under the board. 3D placeholder only: no pads, no nets. */}
    <chip
      name="BT1" {...sch("BT1")}
      layer="bottom"
      pcbX={BATTERY.x}
      pcbY={BATTERY.y}
      doNotPlace
      manufacturerPartNumber="LP801735 400mAh (placeholder)"
      cadModel={<cadmodel modelUrl={batteryModelUrl} modelOriginPosition={{ x: 0, y: 0, z: 0 }} />}
      footprint={
        <footprint>
          <silkscreenrect pcbX={0} pcbY={0} width={BATTERY.length} height={BATTERY.width} strokeWidth={0.15} />
          <silkscreentext text="BT1 LIPO 400mAh" pcbX={0} pcbY={0} fontSize="1.5mm" />
        </footprint>
      }
    />

    {/* ------------------------------------------------------------------ Planes */}
    {/* Stack-up intent: top signal / inner1 GND / inner2 3V3 / bottom signal + GND fill.
        Pours stay out of the antenna keep-out on every layer. */}
    {pours && <copperpour name="POUR_GND_TOP" connectsTo={N.GND} layer="top" clearance="0.25mm" boardEdgeMargin={POUR_EDGE_MARGIN} />}
    {pours && <copperpour name="POUR_GND_L2" connectsTo={N.GND} layer="inner1" clearance="0.25mm" boardEdgeMargin={POUR_EDGE_MARGIN} />}
    {pours && <copperpour name="POUR_3V3_L3" connectsTo={N.V3V3} layer="inner2" clearance="0.25mm" boardEdgeMargin={POUR_EDGE_MARGIN} />}
    {pours && <copperpour name="POUR_GND_BOT" connectsTo={N.GND} layer="bottom" clearance="0.25mm" boardEdgeMargin={POUR_EDGE_MARGIN} />}

    <silkscreentext text="CONCEPT - NOT FOR FAB" pcbX={-13.5} pcbY={11.3} fontSize="1mm" />
    <silkscreentext text="GetMeFit beacon EVT-0" pcbX={-12.5} pcbY={0.3} fontSize="0.8mm" />
    <silkscreentext text="CONCEPT - NOT FOR FAB" layer="bottom" pcbX={-18.5} pcbY={-5.5} fontSize="0.8mm" />
  </board>
)
