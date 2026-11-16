// Bill of materials for the beacon concept board, one row per reference designator.
// Read by Beacon.tsx (supplier part numbers), scripts/make-bom.ts (out/bom.csv) and
// scripts/make-viewer-data.ts (the 3D viewer's callouts and BOM table).
//
// `role` and `why` are the viewer callout texts: what the part does in the beacon, and why
// it sits where it does. Parts without them are listed in the BOM only.

export type BomRow = {
  ref: string
  /** Short description: value or part family. */
  part: string
  /** Manufacturer part number; "-" for generic passives. */
  mpn: string
  package: string
  /** LCSC / JLCPCB part number; "" where there is nothing to buy. */
  lcsc: string
  /** Do not populate by default. */
  dnp: boolean
  /** One-line purpose for the BOM. */
  purpose: string
  role?: string
  why?: string
}

const R0402 = "0402"
const C0402 = "0402"
const C0603 = "0603"

export const BOM: BomRow[] = [
  {
    ref: "U1",
    part: "nRF52840 BLE module, chip antenna",
    mpn: "Raytac MDBT50Q-1MV2",
    package: "LGA-61, 10.5 x 15.5 x 2.05 mm",
    lcsc: "C5118826",
    dnp: false,
    purpose: "MCU and Bluetooth radio (the same module the Feather carries)",
    role: "Runs the firmware and sends the rep counts as BLE advertisements. Pre-certified (FCC/IC/CE), so the radio does not have to be certified from scratch.",
    why: "Antenna end on the long top edge, 0.3 mm inside it. The whole strip along that edge is free of copper and parts on every layer, wider than Raytac's minimum. It sits between the two ends so the USB pair is short on one side and the sensors are on the other.",
  },
  {
    ref: "Y1",
    part: "32.768 kHz crystal, 9 pF",
    mpn: "SF32WK32768D91T004",
    package: "3215, 2 pads",
    lcsc: "C5374762",
    dnp: false,
    purpose: "Low-frequency clock for the BLE stack (not inside the module)",
    role: "Low-frequency clock. The firmware's BLE stack runs from it; the module contains only the 32 MHz crystal.",
    why: "Directly below the module's XL1/XL2 pads, with a 12 pF load capacitor on each side, so the crystal tracks are 4 mm long and need no vias.",
  },
  {
    ref: "U2",
    part: "3-axis accelerometer",
    mpn: "ST LIS3DHTR",
    package: "LGA-16, 3 x 3 x 1 mm",
    lcsc: "C15134",
    dnp: false,
    purpose: "Rep counting (I2C 0x18, INT1 to P1.08, INT2 to P0.07)",
    role: "Counts reps from the motion of the weight stack. Same chip and address (0x18) as the prototype, so the register-level driver is unchanged.",
    why: "Close to mounting hole H2, so it moves rigidly with the stack. Its package edges are parallel to the board edges: sensor +X points to the sensor end, +Y to the bottom edge, +Z up out of the top side.",
  },
  {
    ref: "U3",
    part: "3-axis magnetometer (AMR)",
    mpn: "ST LIS2MDLTR",
    package: "LGA-12, 2 x 2 x 0.7 mm",
    lcsc: "C919695",
    dnp: false,
    purpose: "Primary weight sensor (I2C 0x1E)",
    role: "Estimates the weight lifted from how the steel plates bend the magnetic field: a change of only a few microtesla.",
    why: "At the far end from everything magnetic or current-carrying: the USB-C shell, the battery tabs and leads, the charger and the LDO. Only its own capacitors are near it; the 220 nF one sits right against pins 5 and 6, as ST's datasheet requires. No copper runs under it on the top layer.",
  },
  {
    ref: "U4",
    part: "3-axis Hall sensor",
    mpn: "TI TMAG5273A1QDBVR",
    package: "SOT-23-6",
    lcsc: "C3716049",
    dnp: true,
    purpose: "Alternate weight sensor (I2C 0x35), fitted on EVT boards for comparison",
    role: "The alternate magnetometer. Left unpopulated by default; EVT boards fit both so the two can be compared on the same sets.",
    why: "Beside U3 at the sensor end, 8 mm away, so both sensors see nearly the same field.",
  },
  {
    ref: "U5",
    part: "16 Mbit (2 MB) QSPI NOR flash",
    mpn: "GigaDevice GD25Q16CEIGR",
    package: "USON-8, 3 x 2 mm",
    lcsc: "C2973793",
    dnp: false,
    purpose: "Stores every set's data for later download (cost-down candidate)",
    role: "Stores every set's sensor data until it is downloaded over USB. A cost-down candidate once the set store is no longer needed.",
    why: "Right beside the module's QSPI pads, so the six fast lines stay about 3 mm long.",
  },
  {
    ref: "U6",
    part: "LiPo charger, 4.2 V",
    mpn: "Microchip MCP73831T-2ACI/OT",
    package: "SOT-23-5",
    lcsc: "C424093",
    dnp: false,
    purpose: "Charges the cell from USB at 100 mA",
    role: "Charges the cell from USB at 100 mA (10 k PROG resistor), the same part and current as the Feather.",
    why: "At the power end, between the USB-C connector and the battery connector, so the charge current never flows near the magnetometers.",
  },
  {
    ref: "U7",
    part: "3.3 V LDO, 25 nA quiescent",
    mpn: "TI TPS7A0233PDBVR",
    package: "SOT-23-5",
    lcsc: "C2887324",
    dnp: false,
    purpose: "3.3 V rail; replaces the Feather's AP2112K (55 uA)",
    role: "Makes the 3.3 V rail from the battery. Its 25 nA quiescent current replaces the 55 uA regulator that dominated the Feather's idle draw.",
    why: "At the power end next to the charger and battery connector, keeping the supply current loop small and far from the sensors.",
  },
  {
    ref: "U8",
    part: "USB ESD protection",
    mpn: "ST USBLC6-2SC6",
    package: "SOT-23-6",
    lcsc: "C7519",
    dnp: false,
    purpose: "Protects D+, D- and VBUS at the connector",
    role: "Clamps static discharges on the USB data lines and VBUS before they reach the module.",
    why: "Immediately behind the USB-C connector, in line with the data pair.",
  },
  {
    ref: "J1",
    part: "USB-C receptacle, USB 2.0, 16-pin",
    mpn: "tscircuit usb_c standard (e.g. Korean Hroparts TYPE-C-31-M-12)",
    package: "SMD with through-hole shell stakes",
    lcsc: "C393939",
    dnp: false,
    purpose: "Charging, console, firmware update and set download",
    role: "Charging, the serial console, firmware updates and the set download.",
    why: "On the short edge at the power end. Its steel shell is the largest magnetic part on the board, so it is as far from the magnetometers as the outline allows.",
  },
  {
    ref: "J2",
    part: "JST-PH 2-pin header, side entry",
    mpn: "JST S2B-PH-SM4-TB(LF)(SN)",
    package: "SMD, 2 mm pitch",
    lcsc: "C295747",
    dnp: false,
    purpose: "Battery connector (pin 1 = +, Adafruit polarity, marked on the silkscreen)",
    role: "Battery connector. Plus and minus are the same way round as on Adafruit's Feather, so Adafruit batteries fit. They are also marked on the board, because JST-PH polarity differs between battery vendors.",
    why: "At the power end, opening on the bottom edge, so the battery leads wrap round the edge and never pass the sensors.",
  },
  {
    ref: "J3",
    part: "Tag-Connect TC2030-NL footprint",
    mpn: "footprint only (cable: TC2030-CTX-NL)",
    package: "6 pads + 3 alignment holes",
    lcsc: "",
    dnp: true,
    purpose: "SWD programming in the factory (VDD, SWDIO, SWDCLK, nRESET, GND)",
    role: "Programming pads for the factory: a pogo-pin cable presses on, so there is no connector to buy.",
    why: "On the bottom side, outside the battery's shadow, where a test fixture can reach it with the battery fitted.",
  },
  {
    ref: "D1",
    part: "LED, red",
    mpn: "Hubei KENTO KT-0603R",
    package: "0603",
    lcsc: "C2286",
    dnp: false,
    purpose: "Rep feedback (P1.15, active high)",
    role: "Red LED: blinks on each counted rep.",
    why: "Next to its module pin (P1.15) on the top side, where it is visible.",
  },
  {
    ref: "D2",
    part: "LED, blue",
    mpn: "Everlight 19-217/BHC-ZL1M2RY/3T",
    package: "0603",
    lcsc: "C72041",
    dnp: false,
    purpose: "Bluetooth / console status (P1.10, active high)",
    role: "Blue LED: Bluetooth and console status.",
    why: "Next to its module pin (P1.10) on the top side.",
  },
  {
    ref: "SW1",
    part: "Tactile switch",
    mpn: "Alps Alpine SKRPACE010",
    package: "SMD, 4.2 x 3.2 mm",
    lcsc: "C139797",
    dnp: false,
    purpose: "Reset; also reopens the 10-minute BLE console window",
    role: "Reset button. A reset also reopens the 10-minute BLE console window.",
    why: "On the bottom edge under the module, reachable with a fingertip or pen.",
  },
  {
    ref: "BT1",
    part: "LiPo pouch cell, 400 mAh, 1S",
    mpn: "PKCELL LP801735 (Adafruit #3898)",
    package: "37.0 x 17.5 x 8.2 mm max",
    lcsc: "",
    dnp: true,
    purpose: "Battery. 3D placeholder only: no pads on the board",
    role: "400 mAh battery, shown as a placeholder. It is not soldered to the board; it plugs into J2.",
    why: "Under the board at the power end, tabs and leads toward the USB end. It stops short of the antenna keep-out. Drawn at the largest size its spec sheet allows, its body ends 9.5 mm from the magnetometers.",
  },
  {
    ref: "H1",
    part: "Mounting hole, M2 clearance",
    mpn: "-",
    package: "2.2 mm unplated",
    lcsc: "",
    dnp: true,
    purpose: "Mounting. Use nylon, brass or 316 stainless screws",
    role: "Mounting hole for an M2 screw.",
    why: "Diagonal from H2 and outside the battery's shadow. Use a nylon, brass or 316 stainless screw: an ordinary steel one would disturb the magnetometers.",
  },
  {
    ref: "H2",
    part: "Mounting hole, M2 clearance",
    mpn: "-",
    package: "2.2 mm unplated",
    lcsc: "",
    dnp: true,
    purpose: "Mounting. Use nylon, brass or 316 stainless screws",
    role: "Mounting hole for an M2 screw.",
    why: "Next to the accelerometer, so the sensor follows the stack's motion, and 3 mm clear of the battery's end. Non-magnetic screw only: it is the closest piece of hardware to the magnetometers.",
  },

  // ---- passives ------------------------------------------------------------
  { ref: "C1", part: "12 pF C0G", mpn: "-", package: C0402, lcsc: "C1547", dnp: false, purpose: "Y1 load capacitor (XL1)" },
  { ref: "C2", part: "12 pF C0G", mpn: "-", package: C0402, lcsc: "C1547", dnp: false, purpose: "Y1 load capacitor (XL2)" },
  { ref: "C3", part: "4.7 uF X5R 16 V", mpn: "-", package: C0603, lcsc: "C19666", dnp: false, purpose: "Charger input (VBUS)" },
  { ref: "C4", part: "4.7 uF X5R 16 V", mpn: "-", package: C0603, lcsc: "C19666", dnp: false, purpose: "Charger output (VBAT)" },
  { ref: "C5", part: "1 uF X5R 25 V", mpn: "-", package: C0402, lcsc: "C52923", dnp: false, purpose: "LDO input" },
  { ref: "C6", part: "1 uF X5R 25 V", mpn: "-", package: C0402, lcsc: "C52923", dnp: false, purpose: "LDO output" },
  { ref: "C7", part: "10 uF X5R 10 V", mpn: "-", package: C0603, lcsc: "C19702", dnp: false, purpose: "Module VDD/VDDH bulk (Raytac reference circuit)" },
  { ref: "C8", part: "4.7 uF X5R 16 V", mpn: "-", package: C0603, lcsc: "C19666", dnp: false, purpose: "Module VBUS pin" },
  { ref: "C9", part: "100 nF X7R 16 V", mpn: "-", package: C0402, lcsc: "C1525", dnp: false, purpose: "U8 VBUS pin" },
  { ref: "C10", part: "100 nF X7R 16 V", mpn: "-", package: C0402, lcsc: "C1525", dnp: false, purpose: "Flash decoupling" },
  { ref: "C11", part: "100 nF X7R 16 V", mpn: "-", package: C0402, lcsc: "C1525", dnp: false, purpose: "LIS3DH decoupling" },
  { ref: "C12", part: "10 uF X5R 10 V", mpn: "-", package: C0603, lcsc: "C19702", dnp: false, purpose: "LIS3DH bulk" },
  { ref: "C13", part: "100 nF X7R 16 V", mpn: "-", package: C0402, lcsc: "C1525", dnp: false, purpose: "LIS2MDL decoupling" },
  { ref: "C14", part: "220 nF X7R 16 V", mpn: "-", package: C0402, lcsc: "C16772", dnp: false, purpose: "LIS2MDL C1 pin capacitor" },
  { ref: "C15", part: "100 nF X7R 16 V", mpn: "-", package: C0402, lcsc: "C1525", dnp: true, purpose: "TMAG5273 decoupling (fit with U4)" },
  { ref: "C16", part: "100 nF X7R 16 V", mpn: "-", package: C0402, lcsc: "C1525", dnp: false, purpose: "Battery-sense reservoir at P0.29" },
  { ref: "C17", part: "10 uF X5R 10 V", mpn: "-", package: C0603, lcsc: "C19702", dnp: false, purpose: "LIS2MDL bulk (ST asks for 100 nF + 10 uF at pin 9)" },
  { ref: "R1", part: "2.2 k 1%", mpn: "-", package: R0402, lcsc: "C25879", dnp: false, purpose: "Red LED series resistor (about 0.6 mA)" },
  { ref: "R2", part: "1 k 1%", mpn: "-", package: R0402, lcsc: "C11702", dnp: false, purpose: "Blue LED series resistor (about 0.4 mA)" },
  { ref: "R3", part: "5.1 k 1%", mpn: "-", package: R0402, lcsc: "C25905", dnp: false, purpose: "USB-C CC1 pull-down" },
  { ref: "R4", part: "5.1 k 1%", mpn: "-", package: R0402, lcsc: "C25905", dnp: false, purpose: "USB-C CC2 pull-down" },
  { ref: "R5", part: "27 R 1%", mpn: "-", package: R0402, lcsc: "C25100", dnp: false, purpose: "USB D- series resistor (Raytac reference circuit)" },
  { ref: "R6", part: "27 R 1%", mpn: "-", package: R0402, lcsc: "C25100", dnp: false, purpose: "USB D+ series resistor (Raytac reference circuit)" },
  { ref: "R7", part: "10 k 1%", mpn: "-", package: R0402, lcsc: "C25744", dnp: false, purpose: "Charger PROG: 100 mA" },
  { ref: "R8", part: "1 M 1%", mpn: "-", package: R0402, lcsc: "C26083", dnp: false, purpose: "Battery divider, top" },
  { ref: "R9", part: "1 M 1%", mpn: "-", package: R0402, lcsc: "C26083", dnp: false, purpose: "Battery divider, bottom" },
  { ref: "R10", part: "4.7 k 1%", mpn: "-", package: R0402, lcsc: "C25900", dnp: false, purpose: "I2C SCL pull-up" },
  { ref: "R11", part: "4.7 k 1%", mpn: "-", package: R0402, lcsc: "C25900", dnp: false, purpose: "I2C SDA pull-up" },
  { ref: "R12", part: "10 k 1%", mpn: "-", package: R0402, lcsc: "C25744", dnp: true, purpose: "TMAG5273 INT pull-up (fit with U4)" },
  { ref: "TP1", part: "Test pad", mpn: "-", package: "1.5 mm pad, bottom", lcsc: "", dnp: true, purpose: "VBAT" },
  { ref: "TP2", part: "Test pad", mpn: "-", package: "1.5 mm pad, bottom", lcsc: "", dnp: true, purpose: "3V3" },
  { ref: "TP3", part: "Test pad", mpn: "-", package: "1.5 mm pad, bottom", lcsc: "", dnp: true, purpose: "GND" },
  { ref: "TP4", part: "Test pad", mpn: "-", package: "1.5 mm pad, bottom", lcsc: "", dnp: true, purpose: "SDA" },
  { ref: "TP5", part: "Test pad", mpn: "-", package: "1.5 mm pad, bottom", lcsc: "", dnp: true, purpose: "SCL" },
]

const byRef = new Map(BOM.map((row) => [row.ref, row]))

/** Supplier part number prop for a reference designator, for spreading onto an element. */
export function lcsc(ref: string): { supplierPartNumbers?: { jlcpcb: string[] } } {
  const row = byRef.get(ref)
  if (!row) throw new Error(`bom.ts has no row for ${ref}`)
  return row.lcsc ? { supplierPartNumbers: { jlcpcb: [row.lcsc] } } : {}
}
