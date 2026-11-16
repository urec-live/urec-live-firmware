// Where each part sits on the schematic sheet, in schematic units (one pin pitch is 0.2).
// The sheet is three bands: USB and power across the top, the module and what hangs off it
// in the middle, the sensors and test access along the bottom. Nets are joined by label.

type Spot = { x: number; y: number; section: Section; rotation?: number }
type Section = "USB" | "Power" | "MCU" | "Storage" | "Sensors" | "Debug"

export const SECTIONS: { name: Section; title: string }[] = [
  { name: "USB", title: "USB-C and protection" },
  { name: "Power", title: "Battery, charger, 3.3 V" },
  { name: "MCU", title: "nRF52840 module, clock, reset, LEDs" },
  { name: "Storage", title: "Set store (QSPI flash)" },
  { name: "Sensors", title: "I2C sensors" },
  { name: "Debug", title: "Programming and test pads" },
]

const TOP = 9
const MID = 0
const LOW = -9.5

const SPOTS: Record<string, Spot> = {
  // --- USB ---
  J1: { x: -17, y: TOP, section: "USB" },
  U8: { x: -12.5, y: TOP, section: "USB" },
  R3: { x: -17.6, y: TOP - 3.2, section: "USB" },
  R4: { x: -15.4, y: TOP - 3.2, section: "USB" },
  C9: { x: -12.5, y: TOP - 3.2, section: "USB" },
  R5: { x: -9.6, y: TOP + 0.6, section: "USB" },
  R6: { x: -9.6, y: TOP - 0.6, section: "USB" },

  // --- Power ---
  U6: { x: -4.5, y: TOP, section: "Power" },
  C3: { x: -6.6, y: TOP - 2.4, section: "Power" },
  R7: { x: -4.5, y: TOP - 2.4, section: "Power" },
  C4: { x: -2.4, y: TOP - 2.4, section: "Power" },
  J2: { x: 0.6, y: TOP, section: "Power" },
  BT1: { x: 0.6, y: TOP - 2.4, section: "Power" },
  U7: { x: 5.4, y: TOP, section: "Power" },
  C5: { x: 3.8, y: TOP - 2.4, section: "Power" },
  C6: { x: 7.0, y: TOP - 2.4, section: "Power" },
  R8: { x: 10.6, y: TOP + 0.8, section: "Power" },
  R9: { x: 10.6, y: TOP - 0.4, section: "Power" },
  C16: { x: 10.6, y: TOP - 1.6, section: "Power" },

  // --- MCU ---
  U1: { x: 0, y: MID, section: "MCU" },
  Y1: { x: -6, y: MID + 1.6, section: "MCU" },
  C1: { x: -7.4, y: MID + 0.2, section: "MCU" },
  C2: { x: -4.6, y: MID + 0.2, section: "MCU" },
  C7: { x: -7.4, y: MID - 1.6, section: "MCU" },
  C8: { x: -4.6, y: MID - 1.6, section: "MCU" },
  SW1: { x: -6, y: MID - 3.2, section: "MCU" },
  R1: { x: 4.6, y: MID + 2.4, section: "MCU" },
  D1: { x: 6.8, y: MID + 2.4, section: "MCU" },
  R2: { x: 4.6, y: MID + 1.2, section: "MCU" },
  D2: { x: 6.8, y: MID + 1.2, section: "MCU" },

  // --- Storage ---
  U5: { x: 12, y: MID + 0.6, section: "Storage" },
  C10: { x: 12, y: MID - 1.6, section: "Storage" },

  // --- Sensors ---
  U2: { x: -15, y: LOW, section: "Sensors" },
  C11: { x: -16, y: LOW - 2.4, section: "Sensors" },
  C12: { x: -14, y: LOW - 2.4, section: "Sensors" },
  U3: { x: -8.5, y: LOW, section: "Sensors" },
  C13: { x: -10.4, y: LOW - 2.4, section: "Sensors" },
  C14: { x: -8.5, y: LOW - 2.4, section: "Sensors" },
  C17: { x: -6.6, y: LOW - 2.4, section: "Sensors" },
  U4: { x: -2, y: LOW, section: "Sensors" },
  C15: { x: -3, y: LOW - 2.4, section: "Sensors" },
  R12: { x: -1, y: LOW - 2.4, section: "Sensors" },
  R10: { x: 3, y: LOW + 0.6, section: "Sensors" },
  R11: { x: 3, y: LOW - 0.6, section: "Sensors" },

  // --- Debug ---
  J3: { x: 8.5, y: LOW, section: "Debug" },
  TP1: { x: 12.4, y: LOW + 1.2, section: "Debug" },
  TP2: { x: 12.4, y: LOW + 0.6, section: "Debug" },
  TP3: { x: 12.4, y: LOW, section: "Debug" },
  TP4: { x: 12.4, y: LOW - 0.6, section: "Debug" },
  TP5: { x: 12.4, y: LOW - 1.2, section: "Debug" },
}

/** Schematic placement props for a reference designator, for spreading onto its element. */
export function sch(ref: string): { schX: number; schY: number; schSectionName: string; schRotation?: number } {
  const s = SPOTS[ref]
  if (!s) throw new Error(`schematic-layout.ts has no position for ${ref}`)
  return { schX: s.x, schY: s.y, schSectionName: s.section, ...(s.rotation === undefined ? {} : { schRotation: s.rotation }) }
}
