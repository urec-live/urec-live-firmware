// Prints each component's courtyard and copper extents in board coordinates, and lists
// courtyard overlaps and edge violations. Usage: bun tools/dump-courtyards.ts <circuit.json>
import { readFileSync } from "node:fs"

const cj: any[] = JSON.parse(readFileSync(process.argv[2], "utf8"))
const srcById = new Map(cj.filter((e) => e.type === "source_component").map((e) => [e.source_component_id, e]))
const board = cj.find((e) => e.type === "pcb_board")
type Box = { name: string; layer: string; x0: number; x1: number; y0: number; y1: number }
const boxes: Box[] = []
const f = (n: number) => n.toFixed(2).padStart(7)

for (const pc of cj.filter((e) => e.type === "pcb_component")) {
  const name = srcById.get(pc.source_component_id)?.name ?? "?"
  let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity
  const grow = (x: number, y: number) => {
    x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y)
  }
  for (const e of cj) {
    if (e.pcb_component_id !== pc.pcb_component_id) continue
    if (e.type === "pcb_courtyard_rect") {
      // The record keeps the unrotated width/height; apply the component's rotation here.
      const quarter = Math.abs(Math.round((pc.rotation ?? 0) / 90)) % 2 === 1
      const w = quarter ? e.height : e.width
      const h = quarter ? e.width : e.height
      grow(e.center.x - w / 2, e.center.y - h / 2)
      grow(e.center.x + w / 2, e.center.y + h / 2)
    } else if (e.type === "pcb_courtyard_outline") {
      for (const p of e.outline) grow(p.x, p.y)
    }
  }
  const src = x0 === Infinity ? "bbox" : "court"
  if (x0 === Infinity) {
    grow(pc.center.x - pc.width / 2, pc.center.y - pc.height / 2)
    grow(pc.center.x + pc.width / 2, pc.center.y + pc.height / 2)
  }
  boxes.push({ name, layer: pc.layer, x0, x1, y0, y1 })
  console.log(`${name.padEnd(5)} ${pc.layer.padEnd(6)} ${src}  x[${f(x0)},${f(x1)}] y[${f(y0)},${f(y1)}]  ${f(x1 - x0)} x${f(y1 - y0)}`)
}

console.log("\noverlaps (same layer):")
for (let i = 0; i < boxes.length; i++)
  for (let j = i + 1; j < boxes.length; j++) {
    const a = boxes[i], b = boxes[j]
    if (a.layer !== b.layer) continue
    const ox = Math.min(a.x1, b.x1) - Math.max(a.x0, b.x0)
    const oy = Math.min(a.y1, b.y1) - Math.max(a.y0, b.y0)
    if (ox > 0.001 && oy > 0.001) console.log(`  ${a.name} x ${b.name}: ${ox.toFixed(2)} x ${oy.toFixed(2)}`)
  }

if (board) {
  const hx = board.width / 2, hy = board.height / 2
  console.log(`\nboard ${board.width} x ${board.height}; outside the edge:`)
  for (const b of boxes) {
    const over = Math.max(b.x1 - hx, -hx - b.x0, b.y1 - hy, -hy - b.y0)
    if (over > 0.001) console.log(`  ${b.name}: ${over.toFixed(2)} mm`)
  }
}
