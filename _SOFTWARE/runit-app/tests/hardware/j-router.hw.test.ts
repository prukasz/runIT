import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { CommandError, decodeErrorRecord, deviceCall, removeRecord, requestFrame, runRecord, serviceCall, storeRecord, Watches } from '@backend'
import type { ErrorRecord } from '@backend'
import { defs, openBoard, until } from './harness'
import type { Board } from './harness'

/*
 * How the board runs commands (ROUTER.MD "Commands and answers"), seen from the app over BLE: an urgent command next to a
 * busy work task, the order of pipelined writes, NOREPLY, a whole frame, the error record naming its command, a watch of a
 * GPIO level. Device 0 is the ESP's GPIO (a devkit or the PCB); pins 4 and 5 are free on the devkit.
 */

let board: Board
beforeAll(async () => {
  board = await openBoard()
  for (const pin of [PIN_OUT]) await board.services.device(0, 'IO', 'RESET', { pin }).catch(() => undefined)
})
afterAll(async () => {
  await board.services.device(0, 'IO', 'RESET', { pin: PIN_OUT }).catch(() => undefined)
  await removeRecord(board.link, RECORD).catch(() => undefined)
  await board.close()
})

const PIN_OUT = 5
const RECORD = 91
const BUSY_SCANS = 60 // I2C SCANs of bus 1 in one record: its RUN keeps the work task busy for about a second or more

const errorsSeen = (): ErrorRecord[] => {
  const seen: ErrorRecord[] = []
  board.link.on('errors', (data) => {
    const r = decodeErrorRecord(defs, data)
    if (r) seen.push(r)
  })
  return seen
}

describe('commands on a real board', () => {
  it('an urgent command is answered while a long command holds the work task; a normal one waits behind it', async () => {
    const scan = requestFrame(serviceCall(board.services.catalog, 'I2C', 'SCAN', { bus: 1 }).request)
    await removeRecord(board.link, RECORD).catch(() => undefined)
    await storeRecord(board.link, RECORD, Array(BUSY_SCANS).fill(scan))

    const t0 = performance.now()
    let runAt = 0
    const run = runRecord(board.link, RECORD).then(() => (runAt = performance.now() - t0))
    await new Promise((r) => setTimeout(r, 150)) // the RUN is on the work task by now
    let urgentAt = 0
    let normalAt = 0
    const ping = { keys: [defs.keys.sys!, defs.dir.read, 0x01] }
    const urgent = board.link.request({ ...ping, urgent: true }).then(() => (urgentAt = performance.now() - t0))
    const normal = board.link.request(ping, { timeoutMs: 10_000 }).then(() => (normalAt = performance.now() - t0))
    await Promise.all([run, urgent, normal])

    expect(runAt, 'the RUN took long enough to show anything').toBeGreaterThan(600)
    expect(urgentAt, `urgent at ${urgentAt.toFixed(0)} ms, RUN done at ${runAt.toFixed(0)} ms`).toBeLessThan(runAt - 300)
    expect(normalAt, `normal at ${normalAt.toFixed(0)} ms, RUN done at ${runAt.toFixed(0)} ms`).toBeGreaterThanOrEqual(runAt - 50)
  })

  it('pipelined writes run in the order they were sent: 9 toggles leave the pin high, one more low', async () => {
    const io = (command: string, args: Record<string, unknown>, side?: 'read' | 'write') =>
      board.services.device(0, 'IO', command, args as never, side ? { side } : {})
    await io('MODE', { pin: PIN_OUT, mode: 'SYS_IO_MODE_OUTPUT_PUSH_PULL' }, 'write')
    await io('LEVEL', { pin: PIN_OUT, level: 0 }, 'write')
    await Promise.all(Array.from({ length: 9 }, () => io('TOGGLE', { pin: PIN_OUT })))
    expect(((await io('LEVEL', { pin: PIN_OUT }, 'read')).value as { level: number }).level).toBe(1)
    await io('TOGGLE', { pin: PIN_OUT })
    expect(((await io('LEVEL', { pin: PIN_OUT }, 'read')).value as { level: number }).level).toBe(0)
  })

  it('NOREPLY: done once sent, nothing answered; its failure comes on the errors stream only, naming this client', async () => {
    const seen = errorsSeen()
    const ping = { keys: [defs.keys.sys!, defs.dir.read, 0x01] }
    for (let i = 0; i < 10; i++) expect([...(await board.link.request({ ...ping, noReply: true }))]).toEqual([])
    expect(await board.link.request({ keys: [0xee, 0], noReply: true })).toEqual(new Uint8Array(0))
    const record = await until(() => seen.find((r) => r.command?.request[0] === 0xee), 3000)
    expect(record?.command).toMatchObject({ sender: defs.session.id.app, flags: defs.flags.noReply })
    expect(board.link.state).toBe('connected')
    await board.services.call('SYS', 'PING') // and the link carries on
  })

  it('an error answer and the errors stream carry the same record: this client, the seq, the request', async () => {
    const seen = errorsSeen()
    const error = await board.link.request({ keys: [0xee, 0, 7] }).then(() => undefined, (e: unknown) => e as CommandError)
    expect(error).toBeInstanceOf(CommandError)
    const answered = error!.record!
    expect(answered.source).toBe('command')
    expect(answered.command).toMatchObject({ sender: defs.session.id.app, seq: error!.details.seq })
    expect([...answered.command!.request]).toEqual([0xee, 0, 7])
    const streamed = await until(() => seen.find((r) => r.command?.seq === error!.details.seq && r.command?.request[0] === 0xee), 3000)
    expect(streamed?.chain.map((n) => n.tag)).toEqual(answered.chain.map((n) => n.tag))
  })

  it('a whole frame arrives: a request of the largest size the board takes is read to its last byte', async () => {
    const max = defs.frameMax - defs.frameHead
    const keys = [defs.keys.sys!, defs.dir.read, 0x01] // PING takes no payload: the board says how many bytes it got
    const error = await board.link.request({ keys, payload: new Uint8Array(max - keys.length) }).then(() => undefined, (e: unknown) => e as CommandError)
    expect(error?.record?.cause?.fields).toMatchObject({ got: max - keys.length, need: 0 })
    await expect(board.link.request({ keys, payload: new Uint8Array(max - keys.length + 1) })).rejects.toMatchObject({ code: 'too-long' })
  })

  it('a watch of a GPIO level (ON_CHANGE) sends the new level when the pin toggles, with no request', async () => {
    const watches = new Watches(board.link)
    const levels: number[] = []
    const read = deviceCall(defs, board.services.catalog, 0, 'IO', 'LEVEL', { pin: PIN_OUT }, 'read')
    const stop = await watches.follow(read, { trigger: 'change', periodMs: 20 }, (v) => {
      if (v) levels.push((v as { level: number }).level)
    })
    try {
      expect(await until(() => levels.length >= 1, 2000), 'the first entry').toBeTruthy()
      const before = levels.at(-1)!
      await board.services.device(0, 'IO', 'TOGGLE', { pin: PIN_OUT })
      expect(await until(() => levels.at(-1) === 1 - before, 2000), `levels ${levels}`).toBeTruthy()
      await board.services.device(0, 'IO', 'TOGGLE', { pin: PIN_OUT })
      expect(await until(() => levels.at(-1) === before, 2000), `levels ${levels}`).toBeTruthy()
    } finally {
      await stop()
      watches.close()
    }
  })
})
