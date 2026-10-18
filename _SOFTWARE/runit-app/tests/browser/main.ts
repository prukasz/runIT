import {
  CommandError, DeadMan, ErrorInterpreter, RunitLink, Services, WebBluetoothPlatform, bundledLinkDefinitions, crc32, eraseProject, leaveSafeState, loadProgram, projectInfo,
  projectStream, readProject, saveProject, sendProgram, setAutostart, vmFrame, deviceFrame, deviceRecordId,
} from '@backend'
import type { LinkStatus, ProjectContent } from '@backend'
import { blk, build, engine, num, project, read } from '../engine/helpers'

/* The backend in a real browser on Web Bluetooth: the platform the app ships (`WebBluetoothPlatform`), against the board. The runner
 * (run.mjs) opens this page in Chrome, picks the board in the chooser through the DevTools protocol and calls `runSuite()`. */

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

const IO_SET_LEVEL = 0x0100
const PIN = 22
const defs = bundledLinkDefinitions()

const program = () =>
  build(
    project(
      [num('x'), num('y', 'y', { initial: 3 }), num('z')],
      [blk('e', 'expr', { groups: { inputs: 2 }, inputs: [read('x'), read('y')], outputs: [{ path: { root: 'z', steps: [] } }], editor: { text: 'in0 * 2 + in1' } })],
    ),
  )

async function runSuite(): Promise<Result[]> {
  const results: Result[] = []
  const test = async (name: string, body: () => Promise<void>): Promise<void> => {
    try {
      await body()
      results.push({ name, ok: true })
    } catch (error) {
      results.push({ name, ok: false, detail: error instanceof Error ? `${error.message}` : String(error) })
    }
  }
  const expect = (condition: unknown, message: string): void => {
    if (!condition) throw new Error(message)
  }

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
  await services.call('SYS', 'DEADMAN_SET', { timeout_ms: 0, on_link_lost: false })
  await leaveSafeState(link)
  await services.call('VM', 'RESET')

  await test('PING through the catalog', async () => {
    expect((await uptime()) > 0, 'no uptime')
  })

  await test('50 PINGs in a row, then 40 pipelined', async () => {
    for (let i = 0; i < 50; i++) await services.call('SYS', 'PING')
    expect((await Promise.all(Array.from({ length: 40 }, () => services.call('SYS', 'PING')))).length === 40, 'pipelined')
  })

  await test('every read-only command decodes', async () => {
    for (const [service, command] of [['SYS', 'DEADMAN_GET'], ['BLE', 'STATUS'], ['BLE', 'LIST'], ['CONNECTOR', 'INFO'], ['I2C', 'INFO'], ['LOG', 'GET'], ['POWER_MANAGER', 'GET_CONFIG'], ['POWER_MANAGER', 'GET_STATUS'], ['PROJECT', 'INFO']] as const) {
      await services.call(service, command)
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

  await test('IO on the status LED: TOGGLE twice, read back', async () => {
    const level = async () => String((await services.device(1, 'IO', 'GET_LEVEL', { pin: 23 })).value)
    expect(/High|1|true/i.test(await level()), 'LED is not lit at the start')
    await services.device(1, 'IO', 'TOGGLE', { pin: 23 })
    expect(/Low|0|false/i.test(await level()), 'TOGGLE did not put it out')
    await services.device(1, 'IO', 'TOGGLE', { pin: 23 })
    expect(/High|1|true/i.test(await level()), 'second TOGGLE did not light it')
  })

  await test('a request of 500 bytes goes out in one write, and the link is fine after it', async () => {
    const big = await link.request({ keys: [defs.keys.sys!, 0x01], payload: new Uint8Array(500) }).then(() => 'answered', (e: unknown) => (e instanceof CommandError ? e.code : String(e)))
    expect(big === 'answered' || big === 'device-error', `big frame: ${big}`)
    await uptime() // the link is fine after it
  })

  await test('a program loads, runs, and a runtime write comes back as telemetry', async () => {
    const result = program()
    await loadProgram(link, [...result.frames, engine.exec('normal')])
    const reader = engine.telemetry(result)
    const latest = new Map<string, unknown>()
    const off = link.on('telemetry', (d) => reader.read(d)?.values.forEach((v) => v.variable && latest.set(v.variable, v.value)))
    await sendProgram(link, [engine.subscribe([...result.ids.variables.values()])])
    expect(await until(() => latest.get('y') === 3, 3000), 'no first telemetry')
    const sent = performance.now()
    await sendProgram(link, engine.write(result, [{ variable: 'x', value: 5 }]))
    expect(await until(() => latest.get('z') === 13, 3000), `z is ${String(latest.get('z'))}, not 13`)
    results.push({ name: `  runtime write to telemetry: ${Math.round(performance.now() - sent)} ms`, ok: true })
    off()
    await services.call('VM', 'RESET')
  })

  await test('the dead-man arms and feeds, and disarms', async () => {
    const deadman = new DeadMan(link, { timeoutMs: 1500 })
    await deadman.start()
    expect(((await services.call('SYS', 'DEADMAN_GET')).value as { timeout_ms: number }).timeout_ms === 1500, 'not armed')
    await sleep(3500) // longer than the timeout: only the heartbeat keeps the board out of the safe state
    expect(((await services.device(4, 'DEVICE', 'INFO')).value as { suspended: number }).suspended === 0, 'the board entered its safe state')
    await deadman.stop()
    expect(((await services.call('SYS', 'DEADMAN_GET')).value as { timeout_ms: number }).timeout_ms === 0, 'not disarmed')
  })

  await test('a stored project: save, read back verified, autostart off, erase', async () => {
    const result = program()
    const content: ProjectContent = {
      sections: [{ id: deviceRecordId(1), frames: [deviceFrame(defs, 1, IO_SET_LEVEL, Uint8Array.of(PIN, 1))] }],
      program: [...result.frames, engine.exec('normal')].map((f) => vmFrame(defs, f.op, f.payload)),
    }
    const stream = projectStream(defs, content)
    try {
      expect(JSON.stringify(await saveProject(link, content, { autostart: false })) === JSON.stringify({ bytes: stream.length, crc: crc32(stream) }), 'saved size or CRC')
      expect((await projectInfo(link)).stored, 'not stored')
      expect((await readProject(link))?.verified === true, 'not verified')
    } finally {
      await setAutostart(link, false).catch(() => undefined)
      await eraseProject(link).catch(() => undefined)
      await services.device(1, 'IO', 'SET_LEVEL', { pin: PIN, level: 'Low' }).catch(() => undefined)
    }
    expect((await projectInfo(link)).stored === false, 'not erased')
  })

  await test('the safe state is left again (STOP, then leaveSafeState)', async () => {
    await services.call('SYS', 'STOP')
    expect(((await services.device(4, 'DEVICE', 'INFO')).value as { suspended: number }).suspended === 1, 'not suspended')
    await leaveSafeState(link)
    expect(((await services.device(4, 'DEVICE', 'INFO')).value as { suspended: number }).suspended === 0, 'not resumed')
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
 * A reload drops the link with no goodbye. `beforeReload` leaves the board running a program, armed (dead-man with on_link_lost) or not;
 * the runner reloads the page; `afterReload` connects again with no chooser (the browser remembers the board) and holds what the board
 * did about the loss against what the app can see. */

interface Before {
  readonly uptime: number
}

const reloadWindow = globalThis as { __link?: RunitLink }

/** One plain attempt first (what a user's click does); when the browser still tears the old connection down, the attempts of the reconnect policy. */
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
  const result = program()
  await loadProgram(link, [...result.frames, engine.exec('normal')])
  if (arm) await services.call('SYS', 'DEADMAN_SET', { timeout_ms: 20_000, on_link_lost: true })
  return { uptime: ((await services.call('SYS', 'PING')).value as { uptime_ms: number }).uptime_ms }
}

/** `arm`: the board was left armed with on_link_lost, so the reload put it in its safe state. */
async function afterReload(before: Before, arm: boolean): Promise<Result[]> {
  const results: Result[] = []
  const test = async (name: string, body: () => Promise<void>): Promise<void> => {
    try {
      await body()
      results.push({ name, ok: true })
    } catch (error) {
      results.push({ name, ok: false, detail: error instanceof Error ? error.message : String(error) })
    }
  }
  const expect = (condition: unknown, message: string): void => {
    if (!condition) throw new Error(message)
  }
  const platform = new WebBluetoothPlatform(defs)
  const link = new RunitLink(platform, { timeoutMs: 3000 })
  const services = new Services(link)
  const suspended = async () => ((await services.device(4, 'DEVICE', 'INFO')).value as { suspended: number }).suspended

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
      expect(await until(async () => (await suspended()) === 1, 10_000, 200), 'the devices were not suspended')
    })
    await test('leaveSafeState and the program that was loaded runs again', async () => {
      const result = program()
      await leaveSafeState(link, { start: engine.exec('normal') })
      expect((await suspended()) === 0, 'still suspended')
      const reader = engine.telemetry(result)
      const latest = new Map<string, unknown>()
      const off = link.on('telemetry', (d) => reader.read(d)?.values.forEach((v) => v.variable && latest.set(v.variable, v.value)))
      await sendProgram(link, [engine.subscribe([...result.ids.variables.values()])])
      expect(await until(() => latest.get('y') === 3, 3000), 'the program is gone')
      await sendProgram(link, engine.write(result, [{ variable: 'x', value: 2 }]))
      expect(await until(() => latest.get('z') === 7, 3000), 'the program does not compute')
      off()
    })
  } else {
    await test('nothing was armed: a reload leaves the board out of the safe state', async () => {
      await sleep(4000) // longer than the board takes to hear of the loss
      expect((await suspended()) === 0, 'suspended')
    })
  }

  await test('the board is left disarmed and clean', async () => {
    await services.call('SYS', 'DEADMAN_SET', { timeout_ms: 0, on_link_lost: false })
    await services.call('VM', 'RESET')
    expect(((await services.call('SYS', 'DEADMAN_GET')).value as { timeout_ms: number }).timeout_ms === 0, 'still armed')
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
