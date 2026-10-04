import fs from 'node:fs'
import path from 'node:path'
import type { IncomingMessage, ServerResponse } from 'node:http'
import type { Plugin } from 'vite'

// Dev-server endpoint for the board library: the board creator's templates, one `<id>.board.json` per board in
// components/boards/ (board-canvas-concept.md §9.8). A project keeps its own copy of the board it uses; the library is
// the shared place to start new boards from.
//   GET    /__runit/board-library/            -> JSON list of the file names
//   GET    /__runit/board-library/<id>.board.json
//   PUT    /__runit/board-library/<id>.board.json  (body = the file text)
//   DELETE /__runit/board-library/<id>.board.json

export const BOARD_LIBRARY_ROUTE = '/__runit/board-library/'

const BOARD_FILE = /^[a-z][a-z0-9_]*\.board\.json$/
const MAX_BYTES = 1024 * 1024

/** The path of a library file, or null when the name is not `<id>.board.json` (no folders, no other files). */
export function resolveBoardFile(boardsRoot: string, name: string): string | null {
  if (!BOARD_FILE.test(name)) return null
  const full = path.resolve(boardsRoot, name)
  return full.startsWith(path.resolve(boardsRoot) + path.sep) ? full : null
}

function readBody(req: IncomingMessage): Promise<Buffer> {
  return new Promise((resolve, reject) => {
    const chunks: Buffer[] = []
    let size = 0
    req.on('data', (chunk: Buffer) => {
      size += chunk.length
      if (size > MAX_BYTES) {
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

const send = (res: ServerResponse, status: number, body: string, type = 'text/plain') => {
  res.statusCode = status
  res.setHeader('Content-Type', type)
  res.end(body)
}

export function boardLibrary(boardsRoot: string): Plugin {
  return {
    name: 'runit-board-library',
    apply: 'serve',
    configureServer(server) {
      // The folder is outside the app root and may not exist yet: make it and watch it, so a saved board reaches the bundled list.
      fs.mkdirSync(boardsRoot, { recursive: true })
      server.watcher.add(boardsRoot)
      server.middlewares.use(async (req, res, next) => {
        const url = (req.url ?? '').split('?')[0]
        if (!url.startsWith(BOARD_LIBRARY_ROUTE)) return next()
        const name = decodeURIComponent(url.slice(BOARD_LIBRARY_ROUTE.length))
        try {
          if (!name) {
            if (req.method !== 'GET') return send(res, 405, 'method not allowed')
            return send(res, 200, JSON.stringify(fs.readdirSync(boardsRoot).filter((file) => BOARD_FILE.test(file)).sort()), 'application/json')
          }
          const target = resolveBoardFile(boardsRoot, name)
          if (!target) return send(res, 403, 'path not allowed')
          if (req.method === 'GET') return fs.existsSync(target) ? send(res, 200, fs.readFileSync(target, 'utf8'), 'application/json') : send(res, 404, 'not found')
          if (req.method === 'PUT') {
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
