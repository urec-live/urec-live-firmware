// Checks that the exported CAD files can actually be read, without KiCad or a CAD program:
//   out/beacon.step      loaded with OpenCascade (occt-import-js); solids counted and the
//                        bounding box compared with the board, battery and tallest part.
//   out/beacon-kicad.zip unpacked; circuit.kicad_pcb and circuit.kicad_sch parsed as
//                        S-expressions and their contents counted against the built circuit.
// This proves the files are well-formed and hold the right things. It is not the same as
// opening them in KiCad, which was not done.
// Usage: bun scripts/check-exports.ts      Exits 1 on a mismatch.
import { spawnSync } from "node:child_process"
import { existsSync, mkdirSync, readFileSync, rmSync } from "node:fs"
import { createRequire } from "node:module"
import { dirname, join } from "node:path"
import { fileURLToPath } from "node:url"
import { BATTERY, BOARD, TALLEST_TOP_PART } from "../layout"

const root = join(dirname(fileURLToPath(import.meta.url)), "..")
const out = join(root, "out")
const problems: string[] = []
const f = (n: number) => n.toFixed(2)

const cj: any[] = JSON.parse(readFileSync(join(root, "dist", "index", "circuit.json"), "utf8"))
const count = (type: string) => cj.filter((e) => e.type === type).length

// ---------------------------------------------------------------- STEP
{
  const require = createRequire(import.meta.url)
  const occt = await require("occt-import-js")()
  const result = occt.ReadStepFile(new Uint8Array(readFileSync(join(out, "beacon.step"))), null)
  if (!result.success) {
    problems.push("beacon.step: OpenCascade could not read the file")
  } else {
    const lo = [Infinity, Infinity, Infinity]
    const hi = [-Infinity, -Infinity, -Infinity]
    let triangles = 0
    for (const mesh of result.meshes) {
      const p = mesh.attributes.position.array
      for (let i = 0; i < p.length; i += 3)
        for (let k = 0; k < 3; k++) {
          lo[k] = Math.min(lo[k], p[i + k])
          hi[k] = Math.max(hi[k], p[i + k])
        }
      triangles += mesh.index.array.length / 3
    }
    const size = [0, 1, 2].map((k) => hi[k] - lo[k])
    console.log(`beacon.step: read by OpenCascade, ${result.meshes.length} solids, ${triangles} triangles after meshing`)
    console.log(`  bounding box ${f(size[0])} x ${f(size[1])} x ${f(size[2])} mm  (x ${f(lo[0])}..${f(hi[0])}, y ${f(lo[1])}..${f(hi[1])}, z ${f(lo[2])}..${f(hi[2])})`)
    // Expected: board 50 x 25, plus the battery leads past the +X edge; height = battery + board + tallest part.
    const expectZ = BATTERY.thickness + BOARD.thickness + TALLEST_TOP_PART.height
    console.log(`  expected about ${BOARD.width} (+ battery lead stubs) x ${BOARD.height} x ${f(expectZ)} mm`)
    if (size[0] < BOARD.width - 0.5 || size[0] > BOARD.width + 4) problems.push(`beacon.step: X extent ${f(size[0])} mm is not the board length`)
    if (Math.abs(size[1] - BOARD.height) > 1.5) problems.push(`beacon.step: Y extent ${f(size[1])} mm is not the board width`)
    if (Math.abs(size[2] - expectZ) > 1.0) problems.push(`beacon.step: Z extent ${f(size[2])} mm, expected ${f(expectZ)}`)
    if (result.meshes.length < 40) problems.push(`beacon.step: only ${result.meshes.length} solids`)
  }
}

// ---------------------------------------------------------------- KiCad
/** Minimal S-expression reader: returns nested arrays of strings. Throws on unbalanced input. */
function parseSexpr(text: string): any[] {
  let i = 0
  const read = (): any => {
    const list: any[] = []
    while (i < text.length) {
      const c = text[i]
      if (c === "(") {
        i++
        list.push(read())
      } else if (c === ")") {
        i++
        return list
      } else if (c === '"') {
        let s = ""
        i++
        while (i < text.length && text[i] !== '"') {
          if (text[i] === "\\") i++
          s += text[i++]
        }
        i++
        list.push(s)
      } else if (/\s/.test(c)) {
        i++
      } else {
        let s = ""
        while (i < text.length && !/[\s()]/.test(text[i])) s += text[i++]
        list.push(s)
      }
    }
    return list
  }
  const top = read()
  let depth = 0
  for (const ch of text) {
    if (ch === "(") depth++
    else if (ch === ")") depth--
    if (depth < 0) throw new Error("unbalanced parentheses")
  }
  if (depth !== 0) throw new Error("unbalanced parentheses")
  return top
}
const children = (node: any[], head: string) => node.filter((n) => Array.isArray(n) && n[0] === head)

{
  const zip = join(out, "beacon-kicad.zip")
  const tmp = join(root, "dist", "kicad-check")
  rmSync(tmp, { recursive: true, force: true })
  mkdirSync(tmp, { recursive: true })
  const r = spawnSync("tar", ["-xf", zip, "-C", tmp], { encoding: "utf8" })
  const pcbFile = join(tmp, "circuit.kicad_pcb")
  const schFile = join(tmp, "circuit.kicad_sch")
  if (r.status !== 0 || !existsSync(pcbFile) || !existsSync(schFile)) {
    problems.push(`beacon-kicad.zip: could not unpack circuit.kicad_pcb / circuit.kicad_sch (${(r.stderr || "").trim()})`)
  } else {
    try {
      const pcb = parseSexpr(readFileSync(pcbFile, "utf8"))[0]
      const layers = children(pcb, "layers")[0] ?? []
      const copper = layers.filter((l: any) => Array.isArray(l) && /\.Cu$/.test(l[1])).map((l: any) => l[1])
      const footprints = children(pcb, "footprint").length
      const segments = children(pcb, "segment").length
      const vias = children(pcb, "via").length
      const zones = children(pcb, "zone").length
      const nets = children(pcb, "net").length
      console.log(`\nbeacon-kicad.zip / circuit.kicad_pcb: well-formed (${pcb[0]}, version ${children(pcb, "version")[0]?.[1] ?? "?"})`)
      console.log(`  copper layers: ${copper.join(", ")}`)
      console.log(`  ${footprints} footprints, ${nets} nets, ${segments} track segments, ${vias} vias, ${zones} zones`)
      const wantFootprints = count("pcb_component")
      const wantVias = count("pcb_via")
      console.log(`  built circuit: ${wantFootprints} components, ${wantVias} vias, ${count("pcb_copper_pour")} pour outlines`)
      if (pcb[0] !== "kicad_pcb") problems.push("circuit.kicad_pcb does not start with (kicad_pcb")
      if (copper.length !== BOARD.layers) problems.push(`circuit.kicad_pcb has ${copper.length} copper layers, expected ${BOARD.layers}`)
      if (footprints !== wantFootprints) problems.push(`circuit.kicad_pcb has ${footprints} footprints, the circuit has ${wantFootprints} components`)
      if (vias !== wantVias) problems.push(`circuit.kicad_pcb has ${vias} vias, the circuit has ${wantVias}`)
      if (segments === 0) problems.push("circuit.kicad_pcb has no tracks")

      const sch = parseSexpr(readFileSync(schFile, "utf8"))[0]
      const symbols = children(sch, "symbol").length
      console.log(`beacon-kicad.zip / circuit.kicad_sch: well-formed (${sch[0]}), ${symbols} placed symbols`)
      if (sch[0] !== "kicad_sch") problems.push("circuit.kicad_sch does not start with (kicad_sch")
      if (symbols < 40) problems.push(`circuit.kicad_sch has only ${symbols} symbols`)
    } catch (e: any) {
      problems.push(`KiCad files: ${e.message}`)
    }
  }
}

if (problems.length) {
  console.log(`\nPROBLEMS (${problems.length})`)
  for (const p of problems) console.log(`  ${p}`)
  process.exit(1)
}
console.log("\nexport checks passed")
