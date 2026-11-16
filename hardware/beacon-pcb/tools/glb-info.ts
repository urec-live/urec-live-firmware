// Prints the scene structure of a GLB: nodes, meshes, materials, bounds.
// Usage: bun tools/glb-info.ts <file.glb>
import { readFileSync } from "node:fs"

const buf = readFileSync(process.argv[2])
const jsonLen = buf.readUInt32LE(12)
const json = JSON.parse(buf.subarray(20, 20 + jsonLen).toString("utf8"))
console.log(`bytes=${buf.length} generator=${json.asset?.generator} nodes=${json.nodes?.length} meshes=${json.meshes?.length} materials=${json.materials?.length} images=${json.images?.length ?? 0} textures=${json.textures?.length ?? 0}`)
console.log("scene roots:", json.scenes?.[json.scene ?? 0]?.nodes)
const fmt = (a?: number[]) => (a ? a.map((n) => n.toFixed(2)).join(",") : "-")
for (const [i, n] of (json.nodes ?? []).entries()) {
  const mesh = n.mesh != null ? json.meshes[n.mesh] : null
  let bounds = ""
  if (mesh) {
    const acc = json.accessors[mesh.primitives[0].attributes.POSITION]
    bounds = ` min=[${fmt(acc.min)}] max=[${fmt(acc.max)}] prims=${mesh.primitives.length}`
  }
  console.log(
    `#${i} name=${JSON.stringify(n.name)} mesh=${mesh ? JSON.stringify(mesh.name) : "-"} t=[${fmt(n.translation)}] r=[${fmt(n.rotation)}] s=[${fmt(n.scale)}] children=${JSON.stringify(n.children ?? [])}${bounds} extras=${JSON.stringify(n.extras ?? null)}`,
  )
}
for (const [i, m] of (json.materials ?? []).entries())
  console.log(`mat#${i} ${JSON.stringify(m.name)} base=${fmt(m.pbrMetallicRoughness?.baseColorFactor)} tex=${m.pbrMetallicRoughness?.baseColorTexture ? "yes" : "no"} alpha=${m.alphaMode ?? "OPAQUE"}`)
