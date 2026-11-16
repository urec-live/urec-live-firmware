// Serves viewer/ on http://localhost:4173 for a local look at the 3D page.
// Run: bun scripts/serve-viewer.ts   then open http://localhost:4173/_local.html
import { existsSync, statSync } from "node:fs"
import { dirname, join, normalize } from "node:path"
import { fileURLToPath } from "node:url"

const dir = join(dirname(fileURLToPath(import.meta.url)), "..", "viewer")
const port = Number(process.env.PORT ?? 4173)

Bun.serve({
  port,
  hostname: "127.0.0.1",
  fetch(req) {
    const path = decodeURIComponent(new URL(req.url).pathname)
    const file = normalize(join(dir, path === "/" ? "_local.html" : path))
    if (!file.startsWith(dir) || !existsSync(file) || !statSync(file).isFile()) return new Response("not found", { status: 404 })
    return new Response(Bun.file(file))
  },
})
console.log(`serving ${dir} on http://127.0.0.1:${port}/_local.html`)
