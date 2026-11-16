// Prints every pad/hole of every component in a circuit.json, in component-local
// coordinates (pad position minus component centre), for checking against datasheets.
// Usage: bun tools/dump-pads.ts <circuit.json> [REFDES ...]
import { readFileSync } from "node:fs"

const [file, ...only] = process.argv.slice(2)
const cj: any[] = JSON.parse(readFileSync(file, "utf8"))

const srcById = new Map(cj.filter((e) => e.type === "source_component").map((e) => [e.source_component_id, e]))
const portById = new Map(cj.filter((e) => e.type === "pcb_port").map((e) => [e.pcb_port_id, e]))
const srcPortById = new Map(cj.filter((e) => e.type === "source_port").map((e) => [e.source_port_id, e]))

const r = (n: number) => (Math.round(n * 1000) / 1000).toFixed(3).padStart(8)

for (const pc of cj.filter((e) => e.type === "pcb_component")) {
  const name = srcById.get(pc.source_component_id)?.name ?? "?"
  if (only.length && !only.includes(name)) continue
  console.log(
    `\n=== ${name}  centre=(${r(pc.center.x)},${r(pc.center.y)}) rot=${pc.rotation} layer=${pc.layer} size=${r(pc.width)}x${r(pc.height)}`,
  )
  const rows: string[] = []
  for (const e of cj) {
    if (e.pcb_component_id !== pc.pcb_component_id) continue
    const dx = (e.x ?? e.center?.x ?? 0) - pc.center.x
    const dy = (e.y ?? e.center?.y ?? 0) - pc.center.y
    if (e.type === "pcb_smtpad") {
      const sp = srcPortById.get(portById.get(e.pcb_port_id)?.source_port_id)
      const label = sp ? `${sp.pin_number ?? ""}:${sp.name}` : (e.port_hints ?? []).join("/")
      const dims =
        e.shape === "circle" ? `d=${r(e.radius * 2)}` : `${r(e.width)} x${r(e.height)}${e.shape === "rect" ? "" : " " + e.shape}`
      rows.push(`  pad ${label.padEnd(22)} (${r(dx)},${r(dy)})  ${dims}  ${e.layer}`)
    } else if (e.type === "pcb_plated_hole") {
      const sp = srcPortById.get(portById.get(e.pcb_port_id)?.source_port_id)
      const label = sp ? `${sp.pin_number ?? ""}:${sp.name}` : (e.port_hints ?? []).join("/")
      rows.push(
        `  pth ${label.padEnd(22)} (${r(dx)},${r(dy)})  ${e.shape} hole=${e.hole_diameter ?? `${e.hole_width}x${e.hole_height}`} outer=${e.outer_diameter ?? `${e.outer_width}x${e.outer_height}`}`,
      )
    } else if (e.type === "pcb_hole") {
      rows.push(`  npth                       (${r(dx)},${r(dy)})  d=${e.hole_diameter}`)
    }
  }
  console.log(rows.join("\n"))
}
