// Prints world-space bounds of named nodes in a GLB, per primitive, converted back to
// board coordinates (x_pcb = -X_glb, y_pcb = Z_glb, z = Y_glb).
// Usage: bun tools/glb-bounds.ts <file.glb> NAME [NAME ...]
import { readFileSync } from "node:fs"

const [file, ...names] = process.argv.slice(2)
const buf = readFileSync(file)
const jsonLen = buf.readUInt32LE(12)
const json = JSON.parse(buf.subarray(20, 20 + jsonLen).toString("utf8"))
const f = (n: number) => n.toFixed(2).padStart(7)

function quatRotate(q: number[] | undefined, v: number[]): number[] {
  if (!q) return v
  const [x, y, z, w] = q
  const [vx, vy, vz] = v
  const ix = w * vx + y * vz - z * vy
  const iy = w * vy + z * vx - x * vz
  const iz = w * vz + x * vy - y * vx
  const iw = -x * vx - y * vy - z * vz
  return [ix * w + iw * -x + iy * -z - iz * -y, iy * w + iw * -y + iz * -x - ix * -z, iz * w + iw * -z + ix * -y - iy * -x]
}

for (const node of json.nodes) {
  if (!names.includes(node.name) || node.mesh == null) continue
  const t = node.translation ?? [0, 0, 0]
  const s = node.scale ?? [1, 1, 1]
  console.log(`\n${node.name}: t=[${t.map(f)}] r=${JSON.stringify(node.rotation ?? null)} s=${JSON.stringify(node.scale ?? null)}`)
  for (const [i, prim] of json.meshes[node.mesh].primitives.entries()) {
    const acc = json.accessors[prim.attributes.POSITION]
    const corners: number[][] = []
    for (const x of [acc.min[0], acc.max[0]]) for (const y of [acc.min[1], acc.max[1]]) for (const z of [acc.min[2], acc.max[2]]) corners.push([x, y, z])
    const world = corners.map((c) => quatRotate(node.rotation, [c[0] * s[0], c[1] * s[1], c[2] * s[2]]).map((v, k) => v + t[k]))
    const lo = [0, 1, 2].map((k) => Math.min(...world.map((w) => w[k])))
    const hi = [0, 1, 2].map((k) => Math.max(...world.map((w) => w[k])))
    console.log(`  prim ${i}: pcb x[${f(-hi[0])},${f(-lo[0])}] y[${f(lo[2])},${f(hi[2])}] z[${f(lo[1])},${f(hi[1])}]  mat=${json.materials[prim.material]?.name}`)
  }
}
