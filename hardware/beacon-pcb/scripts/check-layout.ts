// Layout measurements the autorouter does not report by itself:
//   1. USB pair: routed length of D+ against D-, and the skew that implies.
//   2. Crystal nets: routed length and via count.
//   3. Sensor end: which nets have copper tracks inside the magnetometer quiet zone, and
//      whether any charge-path net (VBUS, VBAT) is among them.
//   4. Track widths and via sizes actually used, against the rules in layout.ts.
// Usage: bun scripts/check-layout.ts <circuit.json>     Exits 1 on a rule violation.
import { readFileSync } from "node:fs"
import { MAG, QUIET_ZONE, RULES } from "../layout"

const cj: any[] = JSON.parse(readFileSync(process.argv[2], "utf8"))
const netById = new Map<string, any>(cj.filter((e) => e.type === "source_net").map((n) => [n.source_net_id, n]))
const sourceTrace = new Map<string, any>(cj.filter((e) => e.type === "source_trace").map((t) => [t.source_trace_id, t]))

type Seg = { ax: number; ay: number; bx: number; by: number; layer: string; width: number }
const netOf = (t: any): string => {
  const st = sourceTrace.get(t.source_trace_id)
  const id = st?.connected_source_net_ids?.[0]
  return netById.get(id)?.name ?? "?"
}
const segs = new Map<string, Seg[]>()
const viaCount = new Map<string, number>()
for (const t of cj.filter((e) => e.type === "pcb_trace")) {
  const net = netOf(t)
  const list = segs.get(net) ?? []
  for (let i = 1; i < t.route.length; i++) {
    const a = t.route[i - 1]
    const b = t.route[i]
    if (a.route_type === "wire" && b.route_type === "wire" && a.layer === b.layer)
      list.push({ ax: a.x, ay: a.y, bx: b.x, by: b.y, layer: a.layer, width: Math.min(a.width, b.width) })
  }
  segs.set(net, list)
  viaCount.set(net, (viaCount.get(net) ?? 0) + t.route.filter((p: any) => p.route_type === "via").length)
}
const length = (net: string) => (segs.get(net) ?? []).reduce((s, g) => s + Math.hypot(g.ax - g.bx, g.ay - g.by), 0)
const problems: string[] = []
const mm = (n: number) => `${n.toFixed(2)} mm`

// --- 1. USB pair ---------------------------------------------------------------
// Full-speed USB (12 Mbit/s, all the nRF52840 supports). Signal speed on FR-4 is about
// 6.7 ps/mm. The USB 2.0 full-speed budget for skew between D+ and D- at the device is in
// the hundreds of picoseconds; 100 ps is used here as a strict limit.
const PS_PER_MM = 6.7
const dp = length("USB_DP") + length("USB_DP_MCU")
const dm = length("USB_DM") + length("USB_DM_MCU")
const skewPs = Math.abs(dp - dm) * PS_PER_MM
console.log("USB pair (connector to module, through the series resistors)")
console.log(`  D+ ${mm(dp)}   D- ${mm(dm)}   difference ${mm(Math.abs(dp - dm))} = about ${skewPs.toFixed(0)} ps`)
console.log(`  vias: D+ ${(viaCount.get("USB_DP") ?? 0) + (viaCount.get("USB_DP_MCU") ?? 0)}, D- ${(viaCount.get("USB_DM") ?? 0) + (viaCount.get("USB_DM_MCU") ?? 0)}`)
if (skewPs > 100) problems.push(`USB pair skew ${skewPs.toFixed(0)} ps is over the 100 ps limit used here`)

// --- 2. Crystal ----------------------------------------------------------------
console.log("\n32.768 kHz crystal nets")
for (const net of ["XL1", "XL2"]) {
  const v = viaCount.get(net) ?? 0
  console.log(`  ${net}: ${mm(length(net))}, ${v} vias`)
  if (v > 0) problems.push(`${net} uses ${v} vias`)
  if (length(net) > 10) problems.push(`${net} is longer than 10 mm`)
}

// --- 3. Sensor end -------------------------------------------------------------
const inZone = (x: number, y: number) => x >= QUIET_ZONE.xMin && x <= QUIET_ZONE.xMax && y >= QUIET_ZONE.yMin && y <= QUIET_ZONE.yMax
const zoneLen = new Map<string, number>()
const nearest = new Map<string, number>()
for (const [net, list] of segs) {
  for (const g of list) {
    // sample along the segment
    const n = Math.max(1, Math.ceil(Math.hypot(g.ax - g.bx, g.ay - g.by) / 0.1))
    for (let i = 0; i < n; i++) {
      const x = g.ax + ((g.bx - g.ax) * (i + 0.5)) / n
      const y = g.ay + ((g.by - g.ay) * (i + 0.5)) / n
      if (!inZone(x, y)) continue
      zoneLen.set(net, (zoneLen.get(net) ?? 0) + Math.hypot(g.ax - g.bx, g.ay - g.by) / n)
      for (const p of Object.values(MAG)) nearest.set(net, Math.min(nearest.get(net) ?? Infinity, Math.hypot(x - p.x, y - p.y)))
    }
  }
}
// Nets the sensors themselves need. Everything else in the zone is passing through.
const sensorNets = new Set(["GND", "V3V3", "SDA", "SCL", "MAG_C1", "TMAG_INT"])
console.log(`\nTracks inside the quiet zone (x <= ${QUIET_ZONE.xMax} mm)`)
for (const [net, len] of [...zoneLen].sort((a, b) => b[1] - a[1])) {
  const kind = sensorNets.has(net) ? "sensor net" : "passing through"
  console.log(`  ${net.padEnd(11)} ${mm(len).padStart(9)}   nearest to a magnetometer ${mm(nearest.get(net) ?? 0)}   ${kind}`)
}
for (const net of ["VBUS", "VBAT"]) {
  // TP1 (VBAT test pad) is outside the zone on purpose; no charge-path track belongs in it.
  if ((zoneLen.get(net) ?? 0) > 0) problems.push(`${net} has ${mm(zoneLen.get(net)!)} of track inside the quiet zone`)
}
if (![...zoneLen.keys()].some((n) => n === "VBUS" || n === "VBAT")) console.log("  no VBUS or VBAT track enters the zone")

// --- 4. Widths and vias --------------------------------------------------------
console.log("\nTrack widths used (mm: number of segments)")
for (const net of ["VBUS", "VBAT", "V3V3", "GND"]) {
  const hist = new Map<number, number>()
  for (const g of segs.get(net) ?? []) hist.set(Math.round(g.width * 100) / 100, (hist.get(Math.round(g.width * 100) / 100) ?? 0) + 1)
  console.log(`  ${net.padEnd(5)} ${[...hist].sort((a, b) => b[0] - a[0]).map(([w, n]) => `${w}: ${n}`).join(", ")}`)
}
let narrow = Infinity
for (const list of segs.values()) for (const g of list) narrow = Math.min(narrow, g.width)
console.log(`  narrowest track anywhere: ${narrow} mm (rule: ${RULES.traceWidth})`)
if (narrow < RULES.traceWidth - 1e-6) problems.push(`a track is ${narrow} mm wide, under the ${RULES.traceWidth} mm rule`)
const vias = cj.filter((e) => e.type === "pcb_via")
const viaSizes = [...new Set(vias.map((v) => `${v.outer_diameter}/${v.hole_diameter}`))]
console.log(`  vias: ${vias.length}, pad/hole ${viaSizes.join(", ")} mm (rule: ${RULES.viaPad}/${RULES.viaHole})`)
for (const v of vias)
  if (v.outer_diameter < RULES.viaPad - 1e-6 || v.hole_diameter < RULES.viaHole - 1e-6) {
    problems.push(`via ${v.pcb_via_id} is ${v.outer_diameter}/${v.hole_diameter}, under the rule`)
    break
  }

if (problems.length) {
  console.log(`\nPROBLEMS (${problems.length})`)
  for (const p of problems) console.log(`  ${p}`)
  process.exit(1)
}
console.log("\nall layout checks passed")
