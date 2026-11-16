// Writes out/viewer-data.json: everything the 3D viewer page shows besides the GLB itself.
// Callout texts and the BOM come from bom.ts, zones from layout.ts, part positions from the
// built circuit, and the clearance figures are measured here so the page never hard-codes them.
// Run: bun scripts/make-viewer-data.ts   (after `tsci build`)
import { mkdirSync, readFileSync, writeFileSync } from "node:fs"
import { dirname, join } from "node:path"
import { fileURLToPath } from "node:url"
import { BOM } from "../bom"
import {
  ANTENNA_KEEPOUT,
  ANTENNA_TOP_WINDOW,
  BATTERY,
  BOARD,
  HOLES,
  HOLE_DIAMETER,
  MAG,
  MODULE,
  QUIET_ZONE,
  TALLEST_TOP_PART,
  USB,
} from "../layout"

const root = join(dirname(fileURLToPath(import.meta.url)), "..")
const cj: any[] = JSON.parse(readFileSync(join(root, "dist", "index", "circuit.json"), "utf8"))
const r2 = (n: number) => Math.round(n * 100) / 100

type Rect = { x0: number; x1: number; y0: number; y1: number }
const centred = (x: number, y: number, w: number, h: number): Rect => ({ x0: x - w / 2, x1: x + w / 2, y0: y - h / 2, y1: y + h / 2 })
/** Shortest distance from a point to a rectangle (0 if inside), and the nearest point on it. */
function toRect(px: number, py: number, r: Rect) {
  const nx = Math.min(Math.max(px, r.x0), r.x1)
  const ny = Math.min(Math.max(py, r.y0), r.y1)
  return { distance: Math.hypot(px - nx, py - ny), nearest: { x: r2(nx), y: r2(ny) } }
}

// --- part boxes from the built circuit ---------------------------------------
const srcById = new Map(cj.filter((e) => e.type === "source_component").map((e) => [e.source_component_id, e]))
const boxes = new Map<string, { x: number; y: number; w: number; h: number; layer: string }>()
for (const pc of cj.filter((e) => e.type === "pcb_component")) {
  const name = srcById.get(pc.source_component_id)?.name
  if (!name) continue
  // extent of the component's own pads, holes and courtyard
  let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity
  const grow = (x: number, y: number, hw: number, hh: number) => {
    x0 = Math.min(x0, x - hw); x1 = Math.max(x1, x + hw); y0 = Math.min(y0, y - hh); y1 = Math.max(y1, y + hh)
  }
  for (const e of cj) {
    if (e.pcb_component_id !== pc.pcb_component_id) continue
    if (e.type === "pcb_smtpad") {
      const rot = e.shape === "rotated_rect" && Math.abs((e.ccw_rotation ?? 0) % 180) === 90
      const w = e.shape === "circle" ? e.radius * 2 : rot ? e.height : e.width
      const h = e.shape === "circle" ? e.radius * 2 : rot ? e.width : e.height
      grow(e.x, e.y, w / 2, h / 2)
    } else if (e.type === "pcb_plated_hole" || e.type === "pcb_hole") {
      const d = e.outer_diameter ?? e.hole_diameter ?? 1
      grow(e.x, e.y, d / 2, d / 2)
    }
  }
  if (x0 === Infinity) grow(pc.center.x, pc.center.y, pc.width / 2, pc.height / 2)
  boxes.set(name, { x: r2((x0 + x1) / 2), y: r2((y0 + y1) / 2), w: r2(x1 - x0), h: r2(y1 - y0), layer: pc.layer })
}
// Parts whose real outline is known better from layout.ts than from their pads.
boxes.set("U1", {
  x: MODULE.x + (MODULE.outline.xMin + MODULE.outline.xMax) / 2,
  y: MODULE.y + (MODULE.outline.yMin + MODULE.outline.yMax) / 2,
  w: MODULE.outline.xMax - MODULE.outline.xMin,
  h: MODULE.outline.yMax - MODULE.outline.yMin,
  layer: "top",
})
boxes.set("BT1", { x: BATTERY.x, y: BATTERY.y, w: BATTERY.length, h: BATTERY.width, layer: "bottom" })
for (const [ref, p] of Object.entries(HOLES)) boxes.set(ref, { x: p.x, y: p.y, w: HOLE_DIAMETER + 1.6, h: HOLE_DIAMETER + 1.6, layer: "top" })

// --- clearances from the magnetometers ---------------------------------------
const usbShell: Rect = {
  // J1 is rotated +90 deg: the mouth is at +X. Shell: 5.28 mm ahead of the origin, 2.02 mm behind it.
  x0: USB.x - 2.02,
  x1: USB.x + 5.28,
  y0: USB.y - USB.shellWidth / 2,
  y1: USB.y + USB.shellWidth / 2,
}
const batteryBody = centred(BATTERY.x, BATTERY.y, BATTERY.length, BATTERY.width)
const batteryTabs: Rect = { ...batteryBody, x0: batteryBody.x1 - 2.5 }
const moduleCan: Rect = { x0: MODULE.x - 4.9, x1: MODULE.x + 4.9, y0: MODULE.y - 5.7, y1: MODULE.y + 5.3 }
const partRect = (ref: string): Rect => {
  const b = boxes.get(ref)
  if (!b) throw new Error(`no box for ${ref}`)
  return centred(b.x, b.y, b.w, b.h)
}
const targets: { key: string; label: string; rect: Rect }[] = [
  { key: "usb", label: "USB-C shell (steel)", rect: usbShell },
  { key: "batteryTabs", label: "Battery tabs, protection board and leads", rect: batteryTabs },
  { key: "batteryBody", label: "Battery cell body", rect: batteryBody },
  { key: "charger", label: "Charger U6", rect: partRect("U6") },
  { key: "ldo", label: "LDO U7", rect: partRect("U7") },
  { key: "jst", label: "Battery connector J2", rect: partRect("J2") },
  { key: "moduleCan", label: "Module shield can (DC/DC inductor inside)", rect: moduleCan },
  { key: "h2", label: "Screw head at mounting hole H2", rect: partRect("H2") },
]
const clearances = targets.map((t) => {
  const per = Object.entries(MAG).map(([ref, p]) => ({ ref, ...toRect(p.x, p.y, t.rect) }))
  const worst = per.reduce((a, b) => (a.distance <= b.distance ? a : b))
  return {
    key: t.key,
    label: t.label,
    mm: r2(worst.distance),
    from: { ref: worst.ref, x: (MAG as any)[worst.ref].x, y: (MAG as any)[worst.ref].y },
    to: worst.nearest,
  }
})

// --- assemble ----------------------------------------------------------------
const data = {
  generated: "from bom.ts, layout.ts and dist/index/circuit.json by scripts/make-viewer-data.ts",
  board: { width: BOARD.width, height: BOARD.height, thickness: BOARD.thickness, layers: BOARD.layers },
  stackHeight: {
    // tallest top part + board + battery below
    topParts: TALLEST_TOP_PART.height,
    topPartRef: TALLEST_TOP_PART.ref,
    battery: BATTERY.thickness,
    total: r2(BOARD.thickness + TALLEST_TOP_PART.height + BATTERY.thickness),
  },
  zones: {
    antennaKeepout: { x: ANTENNA_KEEPOUT.x, y: r2(ANTENNA_KEEPOUT.y), w: ANTENNA_KEEPOUT.width, h: r2(ANTENNA_KEEPOUT.depth) },
    antennaTopWindow: { x: r2(ANTENNA_TOP_WINDOW.x), y: r2(ANTENNA_TOP_WINDOW.y), w: ANTENNA_TOP_WINDOW.width, h: ANTENNA_TOP_WINDOW.height },
    quietZone: {
      x: (QUIET_ZONE.xMin + QUIET_ZONE.xMax) / 2,
      y: (QUIET_ZONE.yMin + QUIET_ZONE.yMax) / 2,
      w: QUIET_ZONE.xMax - QUIET_ZONE.xMin,
      h: QUIET_ZONE.yMax - QUIET_ZONE.yMin,
    },
    battery: { x: BATTERY.x, y: BATTERY.y, w: BATTERY.length, h: BATTERY.width, t: BATTERY.thickness },
  },
  clearances,
  parts: BOM.filter((r) => r.role).map((r) => {
    const b = boxes.get(r.ref)
    if (!b) throw new Error(`no position for ${r.ref}`)
    return { ref: r.ref, part: r.part, mpn: r.mpn, package: r.package, lcsc: r.lcsc, dnp: r.dnp, role: r.role, why: r.why, box: b }
  }),
  bom: BOM.map((r) => ({ ref: r.ref, part: r.part, mpn: r.mpn, package: r.package, lcsc: r.lcsc, dnp: r.dnp, purpose: r.purpose })),
}

const out = join(root, "out")
mkdirSync(out, { recursive: true })
writeFileSync(join(out, "viewer-data.json"), JSON.stringify(data, null, 1) + "\n", "utf8")
console.log(`wrote out/viewer-data.json: ${data.parts.length} callouts, ${data.bom.length} BOM rows`)
for (const c of clearances) console.log(`  ${c.label.padEnd(44)} ${c.mm.toFixed(1).padStart(5)} mm from ${c.from.ref}`)
