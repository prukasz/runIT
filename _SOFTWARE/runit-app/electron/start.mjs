import { spawn } from 'node:child_process'
import { fileURLToPath } from 'node:url'
import electron from 'electron'
import { createServer } from 'vite'

// Starts the desktop app. `npm run app`: on the Vite dev server (hot reload; an `npm run dev` already serving the page is reused).
// `npm run app:start` (`--built`): on the built page in dist/, as the installed app runs.
const root = fileURLToPath(new URL('..', import.meta.url))
const URL_DEV = 'http://localhost:5180/'
const built = process.argv.includes('--built')

const running = built || (await fetch(URL_DEV).then(() => true, () => false))
const server = running ? undefined : await createServer({ root })
await server?.listen()

// A terminal inside VS Code (itself Electron) has ELECTRON_RUN_AS_NODE set: Electron would then start as plain Node.
const { ELECTRON_RUN_AS_NODE: _node, ...env } = process.env
const args = [root, ...process.argv.slice(2).filter((arg) => arg !== '--built')]
const child = spawn(electron, args, { stdio: 'inherit', env: built ? env : { ...env, RUNIT_DEV_URL: URL_DEV } })
child.on('exit', async (code) => {
  await server?.close()
  process.exit(code ?? 0)
})
