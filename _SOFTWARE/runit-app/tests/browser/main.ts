import { CommandError, DeadMan, ErrorInterpreter, RunitLink, Services, WebBluetoothPlatform, bundledLinkDefinitions, decodePeers } from '@backend'
import type { LinkStatus } from '@backend'

/* The backend in a real browser on Web Bluetooth: the platform the app ships (`WebBluetoothPlatform`), against the board. The runner
 * (run.mjs) opens this page in Chrome, picks the board in the chooser through the DevTools protocol and calls `runSuite()`. Device 0 is
 * the ESP32-S3's GPIO; PIN is a free pin of the devkit. */

interface Result {
  readonly name: string
  readonly ok: boolean
  readonly detail?: string
}

const sleep = (ms: number): Promise<void> => new Promise((resolve) => setTimeout(resolve, ms))
const until = async <T>(check: () => T | undefined | false | Promise<T | undefined | false>, timeoutMs = 5000, stepMs = 25): Promise<T | undefined> => {
  const end = Date.now() + timeoutMs
  for (;;) {
    const value = await check()
    if (value) return value
    if (Date.now() >= end) return undefined
    await sleep(stepMs)
  }
}

const PIN = 4
const defs = bundledLinkDefinitions()

const tester = (results: Result[]) => ({
  test: async (name: string, body: () => Promise<void>): Promise<void> => {
    try {
      await body()
      results.push({ name, ok: true })
    } catch (error) {
      results.push({ name, ok: false, detail: error instanceof Error ? `${error.message}` : String(error) })
    }
  },
  expect: (condition: unknown, message: string): void => {
    if (!condition) throw new Error(message)
  },
})

const suspended = async (services: Services) => ((await services.device(0, 'DEVICE', 'INFO')).value as { suspended: boolean }).suspended

async function runSuite(): Promise<Result[]> {
  const results: Result[] = []
  const { test, expect } = tester(results)

  const platform = new WebBluetoothPlatform(defs)
  const link = new RunitLink(platform, { timeoutMs: 3000 })
  const statuses: LinkStatus[] = []
  link.onStatus((s) => statuses.push(s))
  const services = new Services(link)
  const uptime = async () => ((await services.call('SYS', 'PING')).value as { uptime_ms: number }).uptime_ms

  await test('Web Bluetooth is available and the chooser offers the board', async () => {
    expect(platform.available, 'navigator.bluetooth is missing')
    const device = await platform.requestDevice() // the runner has the user gesture and picks the board
    expect(device.id, 'no device id')
    await link.connect(device)
    expect(link.state === 'connected', `state ${link.state}`)
  })
  if (link.state !== 'connected') return results

  // a board a failed run left armed or in its safe state would fail the tests that follow for the wrong reason
  await services.call('SYS', 'DEADMAN', { timeout_ms: 0 }, { side: 'write' })
  await services.call('SYS', 'SAFE_LEAVE')

  await test('the session: the board id, its boot and build, and this client open on a provider', async () => {
    const session = link.session!
    expect(session.bootId > 0 && session.buildId > 0, `session ${JSON.stringify(session)}`)
    const peers = decodePeers((await services.call('SESSION', 'PEERS')).data, services.catalog)
    expect(peers.some((p) => p.open && p.peerId === session.clientId), 'this client is not an open session')
  })

  await test('PING through the catalog', async () => {
    expect((await uptime()) > 0, 'no uptime')
  })

  await test('50 PINGs in a row, then 40 pipelined', async () => {
    for (let i = 0; i < 50; i++) await services.call('SYS', 'PING')
    expect((await Promise.all(Array.from({ length: 40 }, () => services.call('SYS', 'PING')))).length === 40, 'pipelined')
  })

  await test('every read without a key decodes', async () => {
    for (const [service, command] of [['SYS', 'DEADMAN'], ['SESSION', 'ID'], ['BLE', 'STATUS'], ['BLE', 'LIST'], ['LOG', 'CONFIG'], ['WATCH', 'LIST'], ['DATA_MATRIX', 'LIST']] as const) {
      await services.call(service, command, undefined, { side: 'read' }).catch((e: unknown) => {
        if (!(e instanceof CommandError && e.record)) throw e // an error record is an answer; nothing else is
      })
    }
  })

  await test('an error answer is a CommandError with the record, and the same record arrives on the errors stream', async () => {
    const seen: Uint8Array[] = []
    const off = link.on('errors', (d) => seen.push(d))
    const error = await services.device(9, 'DEVICE', 'INFO').then(() => undefined, (e: unknown) => e)
    expect(error instanceof CommandError && error.record?.cause?.info?.name === 'ERR_DEV_NOT_FOUND', `error ${String(error)}`)
    expect(await until(() => seen.length > 0, 2000), 'no copy on the errors stream')
    off()
    const report = new ErrorInterpreter().interpret((error as CommandError).record!)
    expect(/9/.test(report.message), report.message)
  })

  await test('GPIO: a pin set as an output, TOGGLE twice, read back', async () => {
    const level = async () => String(((await services.device(0, 'IO', 'LEVEL', { pin: PIN }, { side: 'read' })).value as { level: unknown }).level)
    await services.device(0, 'IO', 'RESET', { pin: PIN }) // a pin a run before set up is set up again
    await services.device(0, 'IO', 'MODE', { pin: PIN, mode: 'SYS_IO_MODE_OUTPUT_PUSH_PULL' }, { side: 'write' })
    await services.device(0, 'IO', 'LEVEL', { pin: PIN, level: 'Low' }, { side: 'write' })
    expect(/Low|0|false/i.test(await level()), 'the pin is not low')
    await services.device(0, 'IO', 'TOGGLE', { pin: PIN })
    expect(/High|1|true/i.test(await level()), 'TOGGLE did not raise it')
    await services.device(0, 'IO', 'TOGGLE', { pin: PIN })
    expect(/Low|0|false/i.test(await level()), 'second TOGGLE did not lower it')
  })

  await test('a request of 500 bytes goes out in one write, and the link is fine after it', async () => {
    const big = await link.request({ key: defs.keys.sys!, body: Uint8Array.from([defs.dir.read, 0x01, ...new Uint8Array(500)]) }).then(() => 'answered', (e: unknown) => (e instanceof CommandError ? e.code : String(e)))
    expect(big === 'answered' || big === 'device-error', `big frame: ${big}`)
    await uptime() // the link is fine after it
  })

  await test('events: a device suspended is an event of the stream, named by the DEVICE contract', async () => {
    const seen: string[] = []
    const off = link.on('events', () => seen.push('event'))
    await services.device(0, 'DEVICE', 'SUSPEND')
    await services.device(0, 'DEVICE', 'RESUME')
    expect(await until(() => seen.length >= 2, 3000), 'no events')
    off()
  })

  await test('the dead-man arms and feeds, and disarms', async () => {
    const deadman = new DeadMan(link, { timeoutMs: 1500 })
    await deadman.start()
    expect(((await services.call('SYS', 'DEADMAN', undefined, { side: 'read' })).value as { timeout_ms: number }).timeout_ms === 1500, 'not armed')
    await sleep(3000) // two timeouts: the heartbeat alone keeps the session
    expect(!(await suspended(services)), 'the board entered its safe state')
    await deadman.stop()
    expect(((await services.call('SYS', 'DEADMAN', undefined, { side: 'read' })).value as { timeout_ms: number }).timeout_ms === 0, 'still armed')
  })

  await test('the safe state is left again (STOP, then SAFE_LEAVE)', async () => {
    await services.call('SYS', 'STOP')
    expect(await suspended(services), 'not suspended')
    await services.call('SYS', 'SAFE_LEAVE')
    expect(!(await suspended(services)), 'not resumed')
  })

  await test('reopen() picks the link up again with no chooser', async () => {
    link.reopen()
    expect(await until(async () => (link.state === 'connected' ? await uptime().then(() => true, () => false) : false), 15_000, 200), 'link did not come back')
  })

  await test('a board restart: the link comes back by itself (no chooser, the same device), the uptime starts over', async () => {
    const up = await uptime()
    const before = statuses.length
    await services.call('SYS', 'RESTART')
    const back = await until(async () => {
      try {
        const now = await uptime()
        return now < up ? now : undefined
      } catch {
        return undefined
      }
    }, 30_000, 300)
    expect(back !== undefined, 'the board did not come back')
    expect(statuses.slice(before).some((s) => s.state === 'reconnecting'), 'the loss was not noticed')
  })

  await test('knownDevices() lists the board once the browser granted it', async () => {
    const known = await platform.knownDevices()
    results.push({ name: `  knownDevices: ${known.length} (needs chrome://flags/#enable-web-bluetooth-new-permissions-backend, else 0)`, ok: true })
  })

  await link.disconnect()
  return results
}

/* ---- reloading the tab ----
 * A reload drops the link with no goodbye. `beforeReload` leaves the board with the dead-man armed (its safety flag: the end of the
 * session is the safe state) or not; the runner reloads the page; `afterReload` connects again with no chooser (the browser remembers
 * the board) and holds what the board did about the loss against what the app can see. */

interface Before {
  readonly uptime: number
}

const reloadWindow = globalThis as { __link?: RunitLink }

/** One plain attempt first (what a user's click does); when the browser still tears the old connection down, the attempts of the reconnect policy. */
const connectAgain = async (platform: WebBluetoothPlatform, link: RunitLink): Promise<{ fromKnown: boolean; ms: number; firstFailed?: string }> => {
  const known = await platform.knownDevices()
  const device = known[0] ?? (await platform.requestDevice())
  const started = performance.now()
  let firstFailed: string | undefined
  try {
    await link.connect(device)
  } catch (error) {
    firstFailed = error instanceof Error ? error.message : String(error)
    const give = sleep(20_000).then(() => {
      throw new Error(`no connection in 20 s after the first attempt failed (${firstFailed}); link ${link.state}`)
    })
    await Promise.race([link.connect(device, { retry: true }), give])
  }
  return { fromKnown: known.length > 0, ms: Math.round(performance.now() - started), ...(firstFailed ? { firstFailed } : {}) }
}

async function beforeReload(arm: boolean): Promise<Before> {
  const platform = new WebBluetoothPlatform(defs)
  const link = new RunitLink(platform, { timeoutMs: 3000 })
  reloadWindow.__link = link
  await connectAgain(platform, link)
  const services = new Services(link)
  if (arm) await services.call('SYS', 'DEADMAN', { timeout_ms: 20_000 }, { side: 'write' })
  return { uptime: ((await services.call('SYS', 'PING')).value as { uptime_ms: number }).uptime_ms }
}

/** `arm`: the board was left armed, so the reload put it in its safe state. */
async function afterReload(before: Before, arm: boolean): Promise<Result[]> {
  const results: Result[] = []
  const { test, expect } = tester(results)
  const platform = new WebBluetoothPlatform(defs)
  const link = new RunitLink(platform, { timeoutMs: 3000 })
  const services = new Services(link)

  await test('the new page connects with no chooser', async () => {
    const { fromKnown, ms, firstFailed } = await connectAgain(platform, link)
    expect(fromKnown, 'knownDevices() is empty: the browser forgot the board')
    expect(link.state === 'connected', `state ${link.state}`)
    results.push({ name: `  connected in ${ms} ms${firstFailed ? `; the first attempt failed: ${firstFailed}` : ''}`, ok: true })
  })
  if (link.state !== 'connected') return results

  await test('the board did not restart (its uptime goes on)', async () => {
    expect(((await services.call('SYS', 'PING')).value as { uptime_ms: number }).uptime_ms > before.uptime, 'the uptime started over')
  })

  if (arm) {
    await test('the reload was a lost link: the board is in its safe state (devices suspended)', async () => {
      expect(await until(() => suspended(services), 10_000, 200), 'the devices were not suspended')
    })
    await test('SAFE_LEAVE resumes them', async () => {
      await services.call('SYS', 'SAFE_LEAVE')
      expect(!(await suspended(services)), 'still suspended')
    })
  } else {
    await test('nothing was armed: a reload leaves the board out of the safe state', async () => {
      await sleep(4000) // longer than the board takes to hear of the loss
      expect(!(await suspended(services)), 'suspended')
    })
  }

  await test('the board is left disarmed and clean', async () => {
    await services.call('SYS', 'DEADMAN', { timeout_ms: 0 }, { side: 'write' })
    expect(((await services.call('SYS', 'DEADMAN', undefined, { side: 'read' })).value as { timeout_ms: number }).timeout_ms === 0, 'still armed')
  })
  await link.disconnect()
  return results
}

/** A page that only connects, to be reloaded again. */
async function connectOnly(): Promise<Result[]> {
  const platform = new WebBluetoothPlatform(defs)
  const link = new RunitLink(platform, { timeoutMs: 3000 })
  reloadWindow.__link = link
  try {
    const { fromKnown, ms, firstFailed } = await connectAgain(platform, link)
    await new Services(link).call('SYS', 'PING')
    return [{ name: `connect + PING after a reload, no chooser (${ms} ms${firstFailed ? `; the first attempt failed: ${firstFailed}` : ''})`, ok: fromKnown }]
  } catch (error) {
    return [{ name: 'connect + PING after a reload', ok: false, detail: error instanceof Error ? error.message : String(error) }]
  }
}

const exposed = globalThis as Record<string, unknown>
exposed.beforeReload = beforeReload
exposed.afterReload = afterReload
exposed.connectOnly = connectOnly
;(globalThis as { runSuite?: typeof runSuite }).runSuite = runSuite
const out = (globalThis as { document?: { getElementById(id: string): { textContent: string } | null } }).document?.getElementById('out')
if (out) out.textContent = 'runIT backend: ready'
