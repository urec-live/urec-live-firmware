// Mechanical layout of the beacon concept board, in millimetres.
// One source of truth: the circuit (index.circuit.tsx), the placeholder 3D models
// (scripts/make-models.ts) and the viewer data (scripts/make-viewer-data.ts) all read it.
//
// Board frame: origin at the board centre, +X to the right, +Y up, seen from the top side.
//   -X end = "sensor end"  (magnetometers; nothing magnetic, no supply currents)
//   +X end = "power end"   (USB-C, battery connector, charger, LDO)
//   +Y edge, centre        = antenna edge (module antenna overhangs nothing, keep-out below it)

export const BOARD = { width: 50, height: 25, thickness: 1.6, layers: 4 } as const

/**
 * Design rules given to the autorouter, in mm. JLCPCB's published 4-layer limits are 0.09 mm
 * track and space, 0.15 mm via hole, 0.25 mm via pad and 0.2 mm copper-to-edge; these sit
 * comfortably inside them (0.15 mm annular ring on vias, JLCPCB's stated minimum for 1 oz).
 */
export const RULES = {
  traceWidth: 0.15,
  /** GND and 3V3 tracks (the planes carry most of the current). */
  powerTraceWidth: 0.25,
  /** VBUS and VBAT: 100 mA charge current. */
  chargeTraceWidth: 0.4,
  viaHole: 0.3,
  viaPad: 0.6,
  /** Copper to board edge. The router keeps track centres 0.35 mm in, so 0.25 is what it can hold. */
  edgeClearance: 0.25,
} as const

/** Half sizes, for edge maths. */
export const EDGE = { x: BOARD.width / 2, y: BOARD.height / 2 } as const

// --- U1: Raytac MDBT50Q-1MV2 -------------------------------------------------
// Footprint frame (Raytac "recommended solder pad layout", spec Ver. K p. 12, re-centred):
// outline x -5.25..5.25, y -6.0..9.5; antenna end at +Y.
/** The module's antenna end stops this far inside the board edge (keeps its courtyard on the board). */
export const MODULE_EDGE_INSET = 0.3
export const MODULE = {
  x: 2,
  // outline top (y + 9.5) sits MODULE_EDGE_INSET inside the board's top edge
  y: EDGE.y - 9.5 - MODULE_EDGE_INSET,
  outline: { xMin: -5.25, xMax: 5.25, yMin: -6.0, yMax: 9.5 },
} as const

/** Raytac's minimum "No ground pad" area (spec Ver. K pp. 9-13): 12.4 mm wide x 3.8 mm deep. */
export const RAYTAC_MIN_KEEPOUT = { width: 12.4, depth: 3.8 } as const

/**
 * Antenna keep-out as built: no copper, no pour, no parts, on EVERY layer.
 * Raytac asks for the no-ground area to be "as wide as you can" and draws it across the whole
 * board edge in its layout example, so here it is the full width of the antenna edge, not the
 * 12.4 mm minimum. Depth: 3.8 mm from the module's antenna end plus the 0.3 mm inset out to
 * the board edge (4.1 mm in total). Mounting hole H1 is inside it; a hole has no copper.
 */
export const ANTENNA_KEEPOUT = {
  width: BOARD.width,
  depth: RAYTAC_MIN_KEEPOUT.depth + MODULE_EDGE_INSET,
  x: 0,
  y: EDGE.y - (RAYTAC_MIN_KEEPOUT.depth + MODULE_EDGE_INSET) / 2,
} as const

/** Raytac "Top layer no ground pad" window: 1.6 x 1.2 mm, 2.95 mm in from the module's left edge. */
export const ANTENNA_TOP_WINDOW = {
  width: 1.6,
  height: 1.2,
  x: MODULE.x + MODULE.outline.xMin + 2.95 + 1.6 / 2,
  y: EDGE.y - MODULE_EDGE_INSET - 3.8 - 1.2 / 2,
} as const

// --- BT1: 400 mAh LiPo pouch, Adafruit #3898 (PKCELL LP801735) ---------------
// Maximum dimensions from the cell's spec sheet (section 14): 37.0 x 17.5 x 8.2 mm including
// the protection board; leads 25 +/- 5 mm with a JST PHR-2 plug. It lies under the board
// (bottom side), pushed to the power end, protection circuit and leads at +X. It stays clear
// of the antenna keep-out.
export const BATTERY = {
  length: 37,
  width: 17.5,
  thickness: 8.2,
  x: EDGE.x - 37 / 2,
  y: -EDGE.y + 1 + 17.5 / 2,
} as const

/**
 * Tallest part on the top side: the JST-PH header J2, 5.5 mm above the board surface
 * (measured from its vendor 3D model in the exported GLB). The USB-C shell is 3.1 mm.
 */
export const TALLEST_TOP_PART = { ref: "J2", height: 5.5 } as const

// --- Mounting holes (M2 clearance) -------------------------------------------
export const HOLE_DIAMETER = 2.2
export const HOLES = {
  H1: { x: 21.8, y: 10.2 },
  // 3 mm clear of the battery's end (x = -12), room for a 4 mm standoff
  H2: { x: -15.0, y: -9.9 },
} as const

/** Test pads on the bottom side, in a row outside the battery's shadow and the antenna keep-out. */
export const TEST_PAD_ROW = { y: 6.6, diameter: 1.5 } as const

// --- Magnetometers -----------------------------------------------------------
export const MAG = {
  U3: { x: -21.5, y: 4 },
  U4: { x: -21.5, y: -4 },
} as const

/**
 * "Quiet zone": the strip at the sensor end that holds only the magnetometers, their
 * decoupling capacitors and bare pads. No connectors, no supply currents, no steel.
 */
export const QUIET_ZONE = { xMin: -EDGE.x, xMax: -17, yMin: -EDGE.y, yMax: EDGE.y } as const

// --- Connectors --------------------------------------------------------------
/** USB-C receptacle. Footprint mouth is at local -Y; rotated +90 deg it faces +X. */
export const USB = { x: 19.47, y: 2.85, rotation: 90, shellLength: 7.3, shellWidth: 8.94 } as const
/** JST-PH side-entry header. Footprint mouth is at local -Y, so unrotated it faces the -Y edge. */
export const JST = { x: 19.7, y: -4.55, rotation: 0 } as const
/** Tag-Connect TC2030-NL, bottom side, outside the battery's shadow. */
export const TAG = { x: -16.5, y: 2.5 } as const
