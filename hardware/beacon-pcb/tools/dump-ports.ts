// Prints the source ports (pin number, name, aliases) of the named components.
// Usage: bun tools/dump-ports.ts <circuit.json> REFDES [REFDES ...]
import { readFileSync } from "node:fs"

const [file, ...only] = process.argv.slice(2)
const cj: any[] = JSON.parse(readFileSync(file, "utf8"))
for (const sc of cj.filter((e) => e.type === "source_component")) {
  if (!only.includes(sc.name)) continue
  console.log(`\n=== ${sc.name} (${sc.ftype ?? ""}) ${JSON.stringify({ mpn: sc.manufacturer_part_number, sup: sc.supplier_part_numbers })}`)
  const ports = cj
    .filter((e) => e.type === "source_port" && e.source_component_id === sc.source_component_id)
    .sort((a, b) => (a.pin_number ?? 0) - (b.pin_number ?? 0))
  for (const p of ports) console.log(`  ${String(p.pin_number).padStart(3)}  ${p.name.padEnd(12)} ${JSON.stringify(p.port_hints)}`)
}
const types = new Map<string, number>()
for (const e of cj) types.set(e.type, (types.get(e.type) ?? 0) + 1)
console.log("\nelement types:", Object.fromEntries(types))
