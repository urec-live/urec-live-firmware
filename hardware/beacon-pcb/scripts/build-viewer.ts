// Assembles the 3D viewer page from viewer/template.html and the exports in out/.
//   viewer/index.html        the page to publish as an Artifact (viewer data inlined)
//   viewer/beacon.gltf.json  out/beacon.glb in glTF JSON form, published beside the page
//   viewer/_local.html       the same page inside a full HTML document, for checking locally
// Run: bun scripts/build-viewer.ts   (after scripts/export-all.ts)
// Local check: bun scripts/serve-viewer.ts, then open http://localhost:4173/_local.html
import { existsSync, mkdirSync, readFileSync, statSync, writeFileSync } from "node:fs"
import { dirname, join } from "node:path"
import { fileURLToPath } from "node:url"

const root = join(dirname(fileURLToPath(import.meta.url)), "..")
const viewer = join(root, "viewer")
const out = join(root, "out")
mkdirSync(join(viewer, "views"), { recursive: true })

const data = readFileSync(join(out, "viewer-data.json"), "utf8")
// "<" is escaped so no part description can ever close the script element.
const inlined = JSON.stringify(JSON.parse(data)).replace(/</g, "\\u003c")
const template = readFileSync(join(viewer, "template.html"), "utf8")
const marker = "/*__VIEWER_DATA__*/null"
if (!template.includes(marker)) throw new Error("template.html has no data marker")
const page = template.replace(marker, inlined)
writeFileSync(join(viewer, "index.html"), page, "utf8")

// The Artifact host does not serve .glb files, so the model is published in glTF's JSON form
// with its binary buffer embedded as a base64 data URI. Same content as out/beacon.glb.
const glb = readFileSync(join(out, "beacon.glb"))
if (glb.readUInt32LE(0) !== 0x46546c67) throw new Error("out/beacon.glb is not a GLB file")
const jsonLength = glb.readUInt32LE(12)
const gltf = JSON.parse(glb.subarray(20, 20 + jsonLength).toString("utf8"))
const binStart = 20 + jsonLength + 8
const binLength = glb.readUInt32LE(20 + jsonLength)
const bin = glb.subarray(binStart, binStart + binLength)
if (gltf.buffers?.length !== 1) throw new Error(`expected one buffer in the GLB, found ${gltf.buffers?.length}`)
gltf.buffers[0].uri = `data:application/octet-stream;base64,${bin.toString("base64")}`
gltf.buffers[0].byteLength = bin.length
const modelFile = join(viewer, "beacon.gltf.json")
writeFileSync(modelFile, JSON.stringify(gltf), "utf8")

// The Artifact host wraps the page in this skeleton when it is published; mirror it locally.
const local = `<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<style>:root{color-scheme:light}body{margin:0;font:14px system-ui,sans-serif}img{max-width:100%}[hidden]{display:none!important}</style>
<script>
  // local check only: ?theme=light or ?theme=dark stands in for the viewer's theme switch
  const theme = new URLSearchParams(location.search).get("theme")
  if (theme) document.documentElement.dataset.theme = theme
</script>
</head>
<body>
${page}
</body>
</html>
`
writeFileSync(join(viewer, "_local.html"), local, "utf8")

const kb = (p: string) => `${(statSync(p).size / 1024).toFixed(0)} KB`
console.log(`viewer/index.html ${kb(join(viewer, "index.html"))}, viewer/beacon.gltf.json ${kb(modelFile)}`)
for (const v of ["iso", "top", "bottom"]) {
  const p = join(viewer, "views", `${v}.png`)
  console.log(`  views/${v}.png ${existsSync(p) ? kb(p) : "MISSING (run scripts/capture-views.ps1)"}`)
}
