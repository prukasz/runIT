import fs from 'node:fs'
import path from 'node:path'
import type { IncomingMessage, ServerResponse } from 'node:http'
import type { Plugin } from 'vite'

// Dev-server endpoint that lets the app write a component's pictures and anchor layouts
// into components/devices/<device_x>/app/canvas/<variant>/ (board-canvas-concept.md §9.13); the pins csv stays in the device root.
//   GET  /__runit/component-files/<device_x>           -> JSON list of the layout + image files under app/canvas/
//   GET  /__runit/component-files/<device_x>/<file>    -> the file
//   PUT  /__runit/component-files/<device_x>/<file>    -> write it (body = file bytes)
//   POST /__runit/component-files/<device_x>/          -> write several files in one request: {files: [{path, text | base64}], remove?: [path]}
//                                                          (the files are written first, then `remove` is applied: a rename)
//                                                          (a saved picture reloads the dev page, so a picture and its layout must not be two requests)
//   DELETE same path                                   -> remove it
// Only app/canvas/<variant>/layout.json and the pictures next to it are writable, so the csv / json that describe the device are never touched.

export const COMPONENT_FILES_ROUTE = '/__runit/component-files/'

const DEVICE_DIR = /^device_[a-z0-9_]+$/
const LAYOUT_FILE = /^app\/canvas\/[a-z][a-z0-9_]*\/layout\.json$/
const IMAGE_FILE = /^app\/canvas\/[a-z][a-z0-9_]*\/[A-Za-z0-9_.-]+\.(svg|png|jpe?g|webp)$/i
const MAX_BYTES = 16 * 1024 * 1024
/** A POST carries pictures as base64 (a third larger). */
const MAX_POST_BYTES = 24 * 1024 * 1024

const MIME: Record<string, string> = {
  '.json': 'application/json',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.jpg': 'image/jpeg',
  '.jpeg': 'image/jpeg',
  '.webp': 'image/webp',
}

/** A writable file's path under the devices root, or null when the request is not one of the allowed shapes. */
export function resolveWritable(devicesRoot: string, device: string, file: string): string | null {
  if (!DEVICE_DIR.test(device)) return null
  if (!LAYOUT_FILE.test(file) && !IMAGE_FILE.test(file)) return null
  if (file.includes('..')) return null
  const full = path.resolve(devicesRoot, device, file)
  const deviceRoot = path.resolve(devicesRoot, device) + path.sep
  return full.startsWith(deviceRoot) ? full : null
}

function listFiles(deviceRoot: string): string[] {
  const out: string[] = []
  const canvas = path.join(deviceRoot, 'app', 'canvas')
  if (!fs.existsSync(canvas)) return out
  for (const variant of fs.readdirSync(canvas)) {
    const dir = path.join(canvas, variant)
    if (!fs.statSync(dir).isDirectory()) continue
    for (const name of fs.readdirSync(dir)) {
      const file = `app/canvas/${variant}/${name}`
      if (LAYOUT_FILE.test(file) || IMAGE_FILE.test(file)) out.push(file)
    }
  }
  return out.sort()
}

function readBody(req: IncomingMessage, limit = MAX_BYTES): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = []
    let size = 0
    req.on('data', (chunk: Buffer) => {
      size += chunk.length
      if (size > limit) {
        reject(new Error('too large'))
        req.destroy()
        return
      }
      chunks.push(chunk)
    })
    req.on('end', () => resolve(Buffer.concat(chunks)))
    req.on('error', reject)
  })
}

function send(res: ServerResponse, status: number, body: string | Buffer, type = 'text/plain') {
  res.statusCode = status
  res.setHeader('Content-Type', type)
  res.end(body)
}

/** Writes every file of a POST body, or none: all paths are checked before the first write. */
function writeMany(res: ServerResponse, devicesRoot: string, device: string, body: Buffer) {
  if (!fs.existsSync(path.join(devicesRoot, device))) return send(res, 404, 'unknown device')
  let files: { path?: unknown; text?: unknown; base64?: unknown }[]
  let remove: unknown[] = []
  try {
    const parsed = JSON.parse(body.toString('utf8')) as { files: typeof files; remove?: unknown[] }
    files = parsed.files
    remove = parsed.remove ?? []
  } catch {
    return send(res, 400, 'bad request')
  }
  if (!Array.isArray(files) || !Array.isArray(remove)) return send(res, 400, 'bad request')
  const removals = remove.map((entry) => (typeof entry === 'string' ? resolveWritable(devicesRoot, device, entry) : null))
  if (removals.some((target) => !target)) return send(res, 403, 'path not allowed')
  const targets = files.map((entry) => (typeof entry.path === 'string' ? resolveWritable(devicesRoot, device, entry.path) : null))
  if (targets.some((target) => !target)) return send(res, 403, 'path not allowed')
  files.forEach((entry, index) => {
    const target = targets[index] as string
    fs.mkdirSync(path.dirname(target), { recursive: true })
    fs.writeFileSync(target, typeof entry.base64 === 'string' ? Buffer.from(entry.base64, 'base64') : String(entry.text ?? ''))
  })
  for (const target of removals) {
    fs.rmSync(target as string, { force: true })
    // A renamed variant leaves its folder empty: drop it.
    const dir = path.dirname(target as string)
    if (fs.existsSync(dir) && fs.readdirSync(dir).length === 0) fs.rmdirSync(dir)
  }
  return send(res, 204, '')
}

export function componentFiles(devicesRoot: string): Plugin {
  return {
    name: 'runit-component-files',
    apply: 'serve',
    configureServer(server) {
      // The folder is outside the app root: watch it so a saved picture or layout reaches the bundled globs without a restart.
      server.watcher.add(devicesRoot)
      server.middlewares.use(async (req, res, next) => {
        const url = (req.url ?? '').split('?')[0]
        if (!url.startsWith(COMPONENT_FILES_ROUTE)) return next()
        const [device, ...rest] = decodeURIComponent(url.slice(COMPONENT_FILES_ROUTE.length)).split('/')
        const file = rest.join('/')
        try {
          if (!file) {
            if (!DEVICE_DIR.test(device)) return send(res, 400, 'bad request')
            if (req.method === 'POST') return writeMany(res, devicesRoot, device, await readBody(req, MAX_POST_BYTES))
            if (req.method !== 'GET') return send(res, 405, 'method not allowed')
            return send(res, 200, JSON.stringify(listFiles(path.join(devicesRoot, device))), MIME['.json'])
          }
          const target = resolveWritable(devicesRoot, device, file)
          if (!target) return send(res, 403, 'path not allowed')
          if (req.method === 'GET') {
            if (!fs.existsSync(target)) return send(res, 404, 'not found')
            return send(res, 200, fs.readFileSync(target), MIME[path.extname(target).toLowerCase()])
          }
          if (req.method === 'PUT') {
            if (!fs.existsSync(path.join(devicesRoot, device))) return send(res, 404, 'unknown device')
            fs.mkdirSync(path.dirname(target), { recursive: true })
            fs.writeFileSync(target, await readBody(req))
            return send(res, 204, '')
          }
          if (req.method === 'DELETE') {
            fs.rmSync(target, { force: true })
            return send(res, 204, '')
          }
          return send(res, 405, 'method not allowed')
        } catch (error) {
          return send(res, 500, error instanceof Error ? error.message : 'error')
        }
      })
    },
  }
}
