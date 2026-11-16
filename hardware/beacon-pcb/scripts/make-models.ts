// Generates the two placeholder 3D models that have no vendor CAD file:
//   models/mdbt50q.glb      Raytac MDBT50Q-1MV2 module (outline from Raytac spec Ver. K, p. 7)
//   models/lipo-400mah.glb  400 mAh LiPo pouch, Adafruit #3898 / LP801735 (36 x 17 x 7.8 mm)
//
// The models are plain boxes. They are in millimetres, in the footprint's own frame
// (X/Y as on the PCB, +Z leaving the board), which is what tscircuit's <cadmodel> expects.
//
// Run: bun scripts/make-models.ts
import { mkdirSync, writeFileSync } from "node:fs"
import { dirname, join } from "node:path"
import { fileURLToPath } from "node:url"
import { BATTERY } from "../layout"

type Box = {
  /** min corner [x, y, z] in mm */
  min: [number, number, number]
  /** max corner [x, y, z] in mm */
  max: [number, number, number]
  /** linear RGB 0..1 */
  color: [number, number, number]
  metallic?: number
  roughness?: number
}

function buildGlb(name: string, boxes: Box[]): Buffer {
  const positions: number[] = []
  const normals: number[] = []
  const primitives: any[] = []
  const materials: any[] = []
  const accessors: any[] = []
  const bufferViews: any[] = []
  const chunks: Buffer[] = []
  let byteOffset = 0

  const pushView = (data: Buffer, target: number) => {
    const padded = data.length % 4 === 0 ? data : Buffer.concat([data, Buffer.alloc(4 - (data.length % 4))])
    bufferViews.push({ buffer: 0, byteOffset, byteLength: data.length, target })
    chunks.push(padded)
    byteOffset += padded.length
    return bufferViews.length - 1
  }

  for (const box of boxes) {
    const [x0, y0, z0] = box.min
    const [x1, y1, z1] = box.max
    // 6 faces, 4 vertices each, with outward normals
    const faces: { n: [number, number, number]; v: [number, number, number][] }[] = [
      { n: [0, 0, 1], v: [[x0, y0, z1], [x1, y0, z1], [x1, y1, z1], [x0, y1, z1]] },
      { n: [0, 0, -1], v: [[x0, y1, z0], [x1, y1, z0], [x1, y0, z0], [x0, y0, z0]] },
      { n: [1, 0, 0], v: [[x1, y0, z0], [x1, y1, z0], [x1, y1, z1], [x1, y0, z1]] },
      { n: [-1, 0, 0], v: [[x0, y1, z0], [x0, y0, z0], [x0, y0, z1], [x0, y1, z1]] },
      { n: [0, 1, 0], v: [[x1, y1, z0], [x0, y1, z0], [x0, y1, z1], [x1, y1, z1]] },
      { n: [0, -1, 0], v: [[x0, y0, z0], [x1, y0, z0], [x1, y0, z1], [x0, y0, z1]] },
    ]
    const pos: number[] = []
    const nor: number[] = []
    const idx: number[] = []
    faces.forEach((f, i) => {
      for (const v of f.v) {
        pos.push(...v)
        nor.push(...f.n)
      }
      const b = i * 4
      idx.push(b, b + 1, b + 2, b, b + 2, b + 3)
    })
    positions.push(...pos)
    normals.push(...nor)

    const posView = pushView(Buffer.from(new Float32Array(pos).buffer), 34962)
    const norView = pushView(Buffer.from(new Float32Array(nor).buffer), 34962)
    const idxView = pushView(Buffer.from(new Uint16Array(idx).buffer), 34963)
    accessors.push(
      { bufferView: posView, componentType: 5126, count: 24, type: "VEC3", min: box.min, max: box.max },
      { bufferView: norView, componentType: 5126, count: 24, type: "VEC3" },
      { bufferView: idxView, componentType: 5123, count: 36, type: "SCALAR" },
    )
    materials.push({
      name: `${name}_${materials.length}`,
      pbrMetallicRoughness: {
        baseColorFactor: [...box.color, 1],
        metallicFactor: box.metallic ?? 0,
        roughnessFactor: box.roughness ?? 0.7,
      },
    })
    const a = accessors.length - 3
    primitives.push({ attributes: { POSITION: a, NORMAL: a + 1 }, indices: a + 2, material: materials.length - 1 })
  }

  const bin = Buffer.concat(chunks)
  const json = {
    asset: { version: "2.0", generator: "getmefit beacon-pcb make-models" },
    scene: 0,
    scenes: [{ nodes: [0] }],
    nodes: [{ name, mesh: 0 }],
    meshes: [{ name, primitives }],
    materials,
    accessors,
    bufferViews,
    buffers: [{ byteLength: bin.length }],
  }
  let jsonBuf = Buffer.from(JSON.stringify(json), "utf8")
  if (jsonBuf.length % 4) jsonBuf = Buffer.concat([jsonBuf, Buffer.alloc(4 - (jsonBuf.length % 4), 0x20)])

  const header = Buffer.alloc(12)
  header.writeUInt32LE(0x46546c67, 0) // "glTF"
  header.writeUInt32LE(2, 4)
  header.writeUInt32LE(12 + 8 + jsonBuf.length + 8 + bin.length, 8)
  const jsonHeader = Buffer.alloc(8)
  jsonHeader.writeUInt32LE(jsonBuf.length, 0)
  jsonHeader.writeUInt32LE(0x4e4f534a, 4) // "JSON"
  const binHeader = Buffer.alloc(8)
  binHeader.writeUInt32LE(bin.length, 0)
  binHeader.writeUInt32LE(0x004e4942, 4) // "BIN"
  return Buffer.concat([header, jsonHeader, jsonBuf, binHeader, bin])
}

const here = dirname(fileURLToPath(import.meta.url))
const outDir = join(here, "..", "models")
mkdirSync(outDir, { recursive: true })

// --- Raytac MDBT50Q-1MV2 -----------------------------------------------------
// Footprint frame: origin at the centre of the pad field; the module outline runs
// x -5.25..5.25, y -6.0..9.5 (antenna end at +Y). Overall height 2.05 mm.
const MODULE_PCB_T = 0.8
writeFileSync(
  join(outDir, "mdbt50q.glb"),
  buildGlb("MDBT50Q", [
    // module substrate
    { min: [-5.25, -6.0, 0], max: [5.25, 9.5, MODULE_PCB_T], color: [0.02, 0.09, 0.16], roughness: 0.6 },
    // shield can over the nRF52840 (below the antenna section)
    { min: [-4.9, -5.7, MODULE_PCB_T], max: [4.9, 5.3, 2.05], color: [0.6, 0.62, 0.64], metallic: 0.3, roughness: 0.45 },
    // chip antenna
    { min: [-3.65, 7.9, MODULE_PCB_T], max: [4.35, 8.9, 1.6], color: [0.88, 0.86, 0.8], roughness: 0.8 },
  ]),
)

// --- 400 mAh LiPo pouch ------------------------------------------------------
// Model frame: centred on the cell in X/Y, z = 0 at the face that touches the board.
// The protection-circuit end (tabs, PCM, leads) is at -X in the model. The part is mounted on
// the bottom layer, which mirrors X, so on the board that end points to +X: the power end.
const { length: L, width: W, thickness: T } = BATTERY
const PCM = 2.5
writeFileSync(
  join(outDir, "lipo-400mah.glb"),
  buildGlb("LiPo400", [
    // pouch cell
    { min: [-L / 2 + PCM, -W / 2, 0], max: [L / 2, W / 2, T], color: [0.5, 0.53, 0.57], metallic: 0.25, roughness: 0.5 },
    // protection circuit strip under kapton tape
    { min: [-L / 2, -W / 2, 1.2], max: [-L / 2 + PCM, W / 2, T - 1.2], color: [0.72, 0.47, 0.1], roughness: 0.6 },
    // lead stubs: red (+) and black (-)
    { min: [-L / 2 - 2.2, 1.2, 3.2], max: [-L / 2, 2.4, 4.4], color: [0.75, 0.08, 0.06], roughness: 0.7 },
    { min: [-L / 2 - 2.2, -2.4, 3.2], max: [-L / 2, -1.2, 4.4], color: [0.04, 0.04, 0.04], roughness: 0.7 },
  ]),
)

console.log(`wrote ${join(outDir, "mdbt50q.glb")} and ${join(outDir, "lipo-400mah.glb")}`)
