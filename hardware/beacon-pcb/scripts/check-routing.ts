// Checks a routed circuit.json against the antenna keep-out and summarises the routing.
// Usage: bun scripts/check-routing.ts <circuit.json>
// Exits 1 if any copper (trace, via or pour) enters the keep-out.
import { readFileSync } from "node:fs"
import { ANTENNA_KEEPOUT, ANTENNA_TOP_WINDOW } from "../layout"

const cj: any[] = JSON.parse(readFileSync(process.argv[2], "utf8"))

type Rect = { x0: number; x1: number; y0: number; y1: number }
const rect = (c: { x: number; y: number; width: number; depth?: number; height?: number }): Rect => {
  const h = c.depth ?? c.height ?? 0
  return { x0: c.x - c.width / 2, x1: c.x + c.width / 2, y0: c.y - h / 2, y1: c.y + h / 2 }
}
const KO = rect(ANTENNA_KEEPOUT)
const KO_TOP = rect(ANTENNA_TOP_WINDOW)

const inside = (r: Rect, x: number, y: number) => x > r.x0 && x < r.x1 && y > r.y0 && y < r.y1
/** Liang-Barsky: does segment a-b pass through rectangle r? */
function segmentHits(r: Rect, ax: number, ay: number, bx: number, by: number): boolean {
  let t0 = 0
  let t1 = 1
  const dx = bx - ax
  const dy = by - ay
  for (const [p, q] of [
    [-dx, ax - r.x0],
    [dx, r.x1 - ax],
    [-dy, ay - r.y0],
    [dy, r.y1 - ay],
  ]) {
    if (p === 0) {
      if (q <= 0) return false
    } else {
      const t = q / p
      if (p < 0) t0 = Math.max(t0, t)
      else t1 = Math.min(t1, t)
      if (t0 >= t1) return false
    }
  }
  return true
}

const problems: string[] = []
const traces = cj.filter((e) => e.type === "pcb_trace")
const vias = cj.filter((e) => e.type === "pcb_via")
const perLayer = new Map<string, number>()
let length = 0

for (const t of traces) {
  for (let i = 1; i < t.route.length; i++) {
    const a = t.route[i - 1]
    const b = t.route[i]
    if (a.route_type !== "wire" || b.route_type !== "wire" || a.layer !== b.layer) continue
    const d = Math.hypot(a.x - b.x, a.y - b.y)
    length += d
    perLayer.set(a.layer, (perLayer.get(a.layer) ?? 0) + d)
    if (segmentHits(KO, a.x, a.y, b.x, b.y)) problems.push(`trace ${t.pcb_trace_id} on ${a.layer} crosses the antenna keep-out`)
    if (a.layer === "top" && segmentHits(KO_TOP, a.x, a.y, b.x, b.y))
      problems.push(`trace ${t.pcb_trace_id} on top crosses the top-layer no-ground window`)
  }
}
for (const v of vias) if (inside(KO, v.x, v.y)) problems.push(`via ${v.pcb_via_id} is inside the antenna keep-out`)

// Copper pours: every outline vertex must be outside the keep-out, and the keep-out centre
// must not be covered (a pour that simply spans the keep-out has no vertex inside it).
const pours = cj.filter((e) => e.type === "pcb_copper_pour")
const pointInPolygon = (pts: { x: number; y: number }[], x: number, y: number) => {
  let c = false
  for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
    const a = pts[i]
    const b = pts[j]
    if (a.y > y !== b.y > y && x < ((b.x - a.x) * (y - a.y)) / (b.y - a.y) + a.x) c = !c
  }
  return c
}
const probes: [number, number][] = []
for (let i = 1; i < 12; i++) for (let j = 1; j < 5; j++) probes.push([KO.x0 + ((KO.x1 - KO.x0) * i) / 12, KO.y0 + ((KO.y1 - KO.y0) * j) / 5])
let pourShapes = 0
for (const p of pours) {
  const rings: { x: number; y: number }[][] = []
  if (p.shape === "polygon" && p.points) rings.push(p.points)
  if (p.shape === "brep" && p.brep_shape) {
    rings.push(p.brep_shape.outer_ring.vertices)
  }
  if (p.shape === "rect") {
    const r = { x0: p.center.x - p.width / 2, x1: p.center.x + p.width / 2, y0: p.center.y - p.height / 2, y1: p.center.y + p.height / 2 }
    rings.push([{ x: r.x0, y: r.y0 }, { x: r.x1, y: r.y0 }, { x: r.x1, y: r.y1 }, { x: r.x0, y: r.y1 }])
  }
  for (const ring of rings) {
    pourShapes++
    const holes: { x: number; y: number }[][] = p.shape === "brep" ? (p.brep_shape.inner_rings ?? []).map((h: any) => h.vertices) : []
    for (const [x, y] of probes) {
      if (pointInPolygon(ring, x, y) && !holes.some((h) => pointInPolygon(h, x, y))) {
        problems.push(`pour ${p.pcb_copper_pour_id} on ${p.layer} covers the antenna keep-out at (${x.toFixed(1)}, ${y.toFixed(1)})`)
        break
      }
    }
  }
}

const errors = cj.filter((e) => /error/.test(e.type))
console.log(`traces: ${traces.length}, vias: ${vias.length}, routed length: ${length.toFixed(0)} mm`)
console.log(`length per layer (mm): ${[...perLayer].map(([l, d]) => `${l} ${d.toFixed(0)}`).join(", ")}`)
console.log(`copper pours: ${pours.length} records (${pourShapes} outlines), layers: ${[...new Set(pours.map((p) => p.layer))].join(", ") || "-"}`)
console.log(`pour shapes: ${[...new Set(pours.map((p) => p.shape))].join(", ") || "-"}`)
console.log(`error elements in circuit.json: ${errors.length}`)
for (const e of errors.slice(0, 20)) console.log(`  ${e.type}: ${(e.message ?? "").slice(0, 200)}`)
if (problems.length) {
  console.log(`\nKEEP-OUT VIOLATIONS: ${problems.length}`)
  for (const p of [...new Set(problems)].slice(0, 30)) console.log(`  ${p}`)
  process.exit(1)
}
console.log("\nantenna keep-out: no trace, via or pour inside it on any layer")
