// Builds the board once and writes every deliverable into out/.
// Run: bun scripts/export-all.ts          (or: npm run export)
//
// Gerbers are deliberately not exported: this is a concept board, not a fabrication package.
import { spawnSync } from "node:child_process"
import { existsSync, mkdirSync, readFileSync, statSync, writeFileSync } from "node:fs"
import { dirname, join } from "node:path"
import { fileURLToPath, pathToFileURL } from "node:url"

const root = join(dirname(fileURLToPath(import.meta.url)), "..")
const out = join(root, "out")
mkdirSync(out, { recursive: true })

const cli = join(root, "node_modules", "tscircuit", "cli.mjs")
const run = (label: string, args: string[]) => {
  const started = Date.now()
  const r = spawnSync(process.execPath, args, { cwd: root, encoding: "utf8" })
  const secs = ((Date.now() - started) / 1000).toFixed(1)
  if (r.status !== 0) {
    console.log(`FAILED  ${label} (${secs} s)\n${(r.stdout + r.stderr).split("\n").slice(-25).join("\n")}`)
    return false
  }
  console.log(`ok      ${label} (${secs} s)`)
  return true
}

const results: [string, boolean][] = []

// 1. Placeholder 3D models, then one build. Every export below reads the built circuit.json,
//    so all outputs describe the same routed board.
results.push(["models", run("placeholder 3D models", [join(root, "scripts", "make-models.ts")])])
results.push(["build", run("tsci build", [cli, "build", "index.circuit.tsx", "--ignore-warnings"])])
const circuitJson = join(root, "dist", "index", "circuit.json")
if (!existsSync(circuitJson)) {
  console.log("no circuit.json: stopping")
  process.exit(1)
}

// The circuit.json refers to the two local placeholder models as "./models/x.glb". The 3D
// exporters only resolve absolute locations, so the 3D exports read a copy with file:// URLs.
const cj: any[] = JSON.parse(readFileSync(circuitJson, "utf8"))
let localModels = 0
for (const e of cj) {
  if (e.type === "cad_component" && typeof e.model_glb_url === "string" && e.model_glb_url.startsWith("./")) {
    e.model_glb_url = pathToFileURL(join(root, e.model_glb_url)).href
    localModels++
  }
}
// tsci only treats a file as circuit JSON when it is named circuit.json, hence the folder.
const circuitJson3d = join(root, "dist", "index-3d", "circuit.json")
mkdirSync(dirname(circuitJson3d), { recursive: true })
writeFileSync(circuitJson3d, JSON.stringify(cj), "utf8")
console.log(`        ${localModels} local 3D models given absolute URLs for the 3D exports`)

const exports: { file: string; format: string; extra?: string[]; source?: string }[] = [
  { file: "beacon.glb", format: "glb", source: circuitJson3d },
  { file: "beacon.step", format: "step", source: circuitJson3d },
  { file: "pcb-top.svg", format: "pcb-svg" },
  { file: "pcb-bottom.svg", format: "pcb-svg", extra: ["--layer", "bottom"] },
  { file: "schematic.svg", format: "schematic-svg" },
  { file: "assembly.svg", format: "assembly-svg" },
  { file: "netlist.txt", format: "readable-netlist" },
  { file: "beacon-kicad.zip", format: "kicad_zip", source: circuitJson3d },
]
for (const e of exports) {
  const target = join(out, e.file)
  const ok = run(e.file, [cli, "export", e.source ?? circuitJson, "-f", e.format, "-o", target, ...(e.extra ?? [])])
  results.push([e.file, ok && existsSync(target) && statSync(target).size > 0])
}

// 2. Outputs generated from this repo's own data.
results.push(["bom.csv", run("bom.csv", [join(root, "scripts", "make-bom.ts")])])
results.push(["viewer-data.json", run("viewer-data.json", [join(root, "scripts", "make-viewer-data.ts")])])
results.push(["routing check", run("routing check", [join(root, "scripts", "check-routing.ts"), circuitJson])])
results.push(["layout check", run("layout check", [join(root, "scripts", "check-layout.ts"), circuitJson])])
results.push(["export check", run("export check", [join(root, "scripts", "check-exports.ts")])])

console.log("\nsummary")
for (const [name, ok] of results) {
  const p = join(out, name)
  const size = existsSync(p) ? `${(statSync(p).size / 1024).toFixed(0)} KB` : ""
  console.log(`  ${ok ? "ok    " : "FAILED"}  ${name.padEnd(20)} ${size}`)
}
process.exit(results.every(([, ok]) => ok) ? 0 : 1)
