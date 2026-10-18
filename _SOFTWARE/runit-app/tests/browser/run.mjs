// Runs tests/browser/main.ts in a real Chrome (Web Bluetooth) against the board: `npm run test:browser`.
// Vite serves the page (localhost is a secure context); the browser is driven over the DevTools protocol, with no dependency:
// Node's own WebSocket. The chooser of Web Bluetooth is answered through the DeviceAccess domain, so nobody has to click.
//
//   CHROME=path to chrome.exe or msedge.exe, HW_NAME=runit (the board's advertised name), CDP_PORT=9333, TIMEOUT_S=180
import { spawn } from 'node:child_process'
import { existsSync, mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'
import { fileURLToPath } from 'node:url'
import { createServer } from 'vite'

const root = fileURLToPath(new URL('../..', import.meta.url))
const at = (p) => fileURLToPath(new URL(p, import.meta.url))
const candidates = [
  process.env.CHROME,
  'C:/Program Files/Google/Chrome/Application/chrome.exe',
  'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
].filter(Boolean)
const chrome = candidates.find((p) => existsSync(p))
if (!chrome) throw new Error('No Chrome or Edge found; set CHROME.')
const port = Number(process.env.CDP_PORT ?? 9333)
const name = (process.env.HW_NAME ?? 'runit').toLowerCase()
const timeoutMs = Number(process.env.TIMEOUT_S ?? 180) * 1000

const server = await createServer({
  root,
  configFile: false,
  logLevel: 'error',
  resolve: { alias: { '@generated': at('../../../generated'), '@engine': at('../../src/engine'), '@backend': at('../../src/backend') } },
  server: { port: 5199, strictPort: true, fs: { allow: [at('../../..')] } },
})
await server.listen()
const url = 'http://localhost:5199/tests/browser/index.html'

const profile = mkdtempSync(join(tmpdir(), 'runit-chrome-'))
const browser = spawn(chrome, [`--remote-debugging-port=${port}`, `--user-data-dir=${profile}`, '--no-first-run', '--no-default-browser-check', '--enable-features=WebBluetoothNewPermissionsBackend', '--enable-experimental-web-platform-features', 'about:blank'], { stdio: 'ignore' })

const cleanup = async () => {
  browser.kill()
  await server.close()
  await new Promise((r) => setTimeout(r, 500))
  try {
    rmSync(profile, { recursive: true, force: true })
  } catch {
    // the browser still holds a file: the temp folder is left
  }
}

let code = 1
try {
  let target
  for (let i = 0; i < 50 && !target; i++) {
    try {
      target = (await (await fetch(`http://127.0.0.1:${port}/json`)).json()).find((t) => t.type === 'page')
    } catch {
      await new Promise((r) => setTimeout(r, 200))
    }
  }
  if (!target) throw new Error('Chrome did not open a page.')

  const ws = new WebSocket(target.webSocketDebuggerUrl)
  await new Promise((resolve, reject) => {
    ws.onopen = resolve
    ws.onerror = reject
  })
  let nextId = 1
  const pending = new Map()
  let giveUp = setTimeout(() => undefined, 0)
  const events = []
  ws.onmessage = (message) => {
    const m = JSON.parse(message.data)
    if (m.id !== undefined) {
      const p = pending.get(m.id)
      pending.delete(m.id)
      if (m.error) p?.reject(new Error(`${m.error.message}`))
      else p?.resolve(m.result)
    } else {
      events.push(m)
      if (m.method === 'DeviceAccess.deviceRequestPrompted') {
        // the event comes again as the scan finds boards: pick the board when it shows up, give up after a while
        const { id, devices } = m.params
        const pick = devices.find((d) => (d.name ?? '').toLowerCase() === name)
        if (pick) {
          console.log(`chooser: ${pick.name}`)
          clearTimeout(giveUp)
          void send('DeviceAccess.selectPrompt', { id, deviceId: pick.id })
        } else if (!giveUp._idleTimeout || giveUp._destroyed) {
          giveUp = setTimeout(() => {
            console.log(`chooser: no board named ${name} in 20 s (saw: ${devices.map((d) => d.name || d.id).join(', ') || 'nothing'})`)
            void send('DeviceAccess.cancelPrompt', { id })
          }, 20_000)
        }
      } else if (m.method === 'Runtime.exceptionThrown') console.log('page exception:', m.params.exceptionDetails.text, m.params.exceptionDetails.exception?.description ?? '')
      else if (m.method === 'Runtime.consoleAPICalled' && m.params.type === 'error') console.log('page console.error:', m.params.args.map((a) => a.value ?? a.description).join(' '))
    }
  }
  const send = (method, params = {}) =>
    new Promise((resolve, reject) => {
      const id = nextId++
      pending.set(id, { resolve, reject })
      ws.send(JSON.stringify({ id, method, params }))
    })

  await send('Page.enable')
  await send('Runtime.enable')
  await send('DeviceAccess.enable')
  await send('Page.navigate', { url })
  for (let i = 0; i < 100; i++) {
    const ready = await send('Runtime.evaluate', { expression: "typeof window.runSuite === 'function'", returnByValue: true })
    if (ready.result.value) break
    await new Promise((r) => setTimeout(r, 200))
  }

  const run = send('Runtime.evaluate', { expression: 'window.runSuite()', awaitPromise: true, returnByValue: true, userGesture: true })
  const results = await Promise.race([run, new Promise((_, reject) => setTimeout(() => reject(new Error(`no result in ${timeoutMs / 1000} s`)), timeoutMs))])
  if (results.exceptionDetails) throw new Error(results.exceptionDetails.exception?.description ?? results.exceptionDetails.text)
  let failed = 0
  let total = 0
  for (const r of results.result.value) {
    const counted = !r.name.startsWith('  ')
    if (counted) total++
    if (counted && !r.ok) failed++
    console.log(`${r.ok ? 'PASS' : 'FAIL'}  ${r.name.trim() === r.name ? r.name : r.name.trim()}${r.ok ? '' : `  -- ${r.detail}`}`)
  }

  // Reloading the tab: the link drops with no goodbye. The page that comes up must find the board again by itself.
  const reload = async () => {
    await send('Page.reload')
    await new Promise((r) => setTimeout(r, 300))
    for (let i = 0; i < 100; i++) {
      try {
        const ready = await send('Runtime.evaluate', { expression: "typeof window.afterReload === 'function'", returnByValue: true })
        if (ready.result.value) return
      } catch {
        // the page is being replaced
      }
      await new Promise((r) => setTimeout(r, 200))
    }
  }
  const call = async (expression, limitMs = 90_000) => {
    const run = send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true, userGesture: true })
    const r = await Promise.race([run, new Promise((_, reject) => setTimeout(() => reject(new Error(`${expression.slice(0, 40)} did not finish in ${limitMs / 1000} s`)), limitMs))])
    if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text)
    return r.result.value
  }
  const show = (label, list) => {
    for (const r of list) {
      const counted = !r.name.startsWith('  ')
      if (counted) total++
      if (counted && !r.ok) failed++
      console.log(`${r.ok ? 'PASS' : 'FAIL'}  [${label}] ${r.name.trim()}${r.ok ? '' : `  -- ${r.detail ?? ''}`}`)
    }
  }
  for (const [label, arm] of [['reload, board armed', true], ['reload, nothing armed', false]]) {
    const before = await call(`window.beforeReload(${arm})`)
    await reload()
    show(label, await call(`window.afterReload(${JSON.stringify(before)}, ${arm})`))
  }
  for (let i = 1; i <= 3; i++) {
    await reload()
    show(`reload ${i} of 3 in a row`, await call('window.connectOnly()'))
  }
  console.log(`\n${total - failed}/${total} passed (${chrome.includes('msedge') ? 'Edge' : 'Chrome'})`)
  code = failed === 0 && total > 0 ? 0 : 1
  ws.close()
} catch (error) {
  console.error(error)
} finally {
  await cleanup()
}
process.exit(code)
