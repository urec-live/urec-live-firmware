// Writes out/bom.csv from bom.ts and cross-checks it against the built circuit:
// every component on the board must have a BOM row, and the DNP flags must agree.
// Run: bun scripts/make-bom.ts   (after `tsci build`)
import { existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs"
import { dirname, join } from "node:path"
import { fileURLToPath } from "node:url"
import { BOM } from "../bom"

const root = join(dirname(fileURLToPath(import.meta.url)), "..")
const out = join(root, "out")
mkdirSync(out, { recursive: true })

const csvCell = (s: string) => (/[",\n]/.test(s) ? `"${s.replace(/"/g, '""')}"` : s)
const header = ["refdes", "part", "mpn", "package", "lcsc", "dnp", "purpose"]
const lines = [header.join(",")]
for (const r of BOM) lines.push([r.ref, r.part, r.mpn, r.package, r.lcsc, r.dnp ? "DNP" : "", r.purpose].map(csvCell).join(","))
writeFileSync(join(out, "bom.csv"), lines.join("\n") + "\n", "utf8")

const populated = BOM.filter((r) => !r.dnp).length
console.log(`wrote out/bom.csv: ${BOM.length} rows, ${populated} populated, ${BOM.length - populated} DNP or nothing to place`)

// Cross-check against the built circuit.
const cjPath = join(root, "dist", "index", "circuit.json")
if (!existsSync(cjPath)) {
  console.log("dist/index/circuit.json not found: skipped the cross-check (run tsci build first)")
  process.exit(0)
}
const cj: any[] = JSON.parse(readFileSync(cjPath, "utf8"))
// tscircuit records doNotPlace on the pcb_component, not on the source_component.
const doNotPlace = new Set(cj.filter((e) => e.type === "pcb_component" && e.do_not_place).map((e) => e.source_component_id))
const onBoard = new Map<string, any>(
  cj.filter((e) => e.type === "source_component").map((e) => [e.name, { ...e, do_not_place: doNotPlace.has(e.source_component_id) }]),
)
const holes = cj.filter((e) => e.type === "pcb_hole" && !e.pcb_component_id).length
const problems: string[] = []
for (const name of onBoard.keys()) if (!BOM.some((r) => r.ref === name)) problems.push(`${name} is on the board but not in bom.ts`)
for (const r of BOM) {
  if (/^H\d+$/.test(r.ref)) continue // mounting holes are not components
  const c = onBoard.get(r.ref)
  if (!c) {
    problems.push(`${r.ref} is in bom.ts but not on the board`)
    continue
  }
  const boardLcsc: string[] = c.supplier_part_numbers?.jlcpcb ?? []
  if (r.lcsc && !boardLcsc.includes(r.lcsc)) problems.push(`${r.ref}: bom.ts says ${r.lcsc}, the board says ${boardLcsc.join("/") || "none"}`)
  // Parts that are fitted must not be flagged do_not_place on the board, and vice versa.
  const fitted = !r.dnp
  if (fitted && c.do_not_place) problems.push(`${r.ref}: fitted in bom.ts but doNotPlace on the board`)
  if (!fitted && r.lcsc && !c.do_not_place) problems.push(`${r.ref}: DNP in bom.ts but not doNotPlace on the board`)
}
const bomHoles = BOM.filter((r) => /^H\d+$/.test(r.ref)).length
if (holes !== bomHoles) problems.push(`board has ${holes} mounting holes, bom.ts lists ${bomHoles}`)

if (problems.length) {
  console.log(`BOM / board mismatches (${problems.length}):`)
  for (const p of problems) console.log(`  ${p}`)
  process.exit(1)
}
console.log(`cross-check passed: ${onBoard.size} components and ${holes} mounting holes on the board all have BOM rows`)
