// Prints the cad_component (3D model) record of every component.
// Usage: bun tools/dump-cad.ts <circuit.json>
import { readFileSync } from "node:fs"

const cj: any[] = JSON.parse(readFileSync(process.argv[2], "utf8"))
const srcById = new Map(cj.filter((e) => e.type === "source_component").map((e) => [e.source_component_id, e]))
for (const c of cj.filter((e) => e.type === "cad_component")) {
  const name = srcById.get(c.source_component_id)?.name ?? "?"
  const { type, cad_component_id, pcb_component_id, source_component_id, ...rest } = c
  console.log(name.padEnd(5), JSON.stringify(rest))
}
