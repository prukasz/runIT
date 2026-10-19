import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { CommandError, ErrorInterpreter, ErrorLog, combineResolvers, deviceRefs } from '@backend'
import { openBoard, sleep, until } from './harness'
import type { Board } from './harness'

/* Errors as a real board sends them: the answer and the errors stream, read for a user. BACKEND.md listed these as read from
 * records built from errors.json only. */

const interpreter = new ErrorInterpreter({ resolve: combineResolvers(deviceRefs(new Map([[1, 'Expander'], [4, 'Analog out']]))) })
let board: Board
let log: ErrorLog
beforeAll(async () => {
  board = await openBoard()
  log = new ErrorLog(interpreter, { capacity: 50 })
  log.attach(board.link)
})
afterAll(async () => {
  log.close()
  await board.close()
})

const fail = async (run: () => Promise<unknown>): Promise<CommandError> => {
  const error = await run().then(
    () => undefined,
    (e: unknown) => e,
  )
  expect(error).toBeInstanceOf(CommandError)
  return error as CommandError
}

describe('an error from the board, read for a user', () => {
  it('names the device and the pin and says what is wrong', async () => {
    const error = await fail(() => board.services.device(1, 'IO', 'LEVEL', { pin: 99, level: 'High' }, { side: 'write' }))
    const report = interpreter.interpret(error.record!)
    expect(report.message).toMatch(/pin/i)
    expect(report.message).toContain('99')
    expect(report.refs.map((r) => `${r.kind}:${r.id}`)).toEqual(expect.arrayContaining(['device:1']))
    expect(report.where.join(' | ')).toContain('Expander') // the app's name, never an obj-... id
    expect(report.command?.label).toMatch(/IO/)
  })

  it('a device that is not there is about that device', async () => {
    const error = await fail(() => board.services.device(9, 'DEVICE', 'INFO'))
    const report = interpreter.interpret(error.record!)
    expect(report.message).toMatch(/9/)
    expect(report.refs.some((r) => r.kind === 'device' && r.id === 9)).toBe(true)
  })

  it('every node of the chain is a tag errors.json knows', async () => {
    const error = await fail(() => board.services.device(4, 'IO', 'VOLTAGE', { pin: 5, voltage_mV: 100 }, { side: 'write' }))
    const report = interpreter.interpret(error.record!)
    expect(report.chain.length).toBeGreaterThan(1)
    for (const node of report.chain) expect(node.message, node.name).toBeTruthy()
    expect(report.chain.every((n) => (n as { known?: boolean }).known !== false)).toBe(true)
  })

  it('an unknown router key', async () => {
    const error = await fail(() => board.link.request({ keys: [0xee] }))
    const report = interpreter.interpret(error.record!)
    expect(report.message.length).toBeGreaterThan(0)
  })
})

describe('the error log', () => {
  it('one failed command is one entry: the answer and the stream copy are the same error', async () => {
    log.clear()
    const error = await fail(() => board.services.device(1, 'IO', 'LEVEL', { pin: 98, level: 'Low' }, { side: 'write' }))
    log.addCommandError(error)
    await sleep(500) // the stream copy
    const entries = log.entries({ ref: 'device:1' })
    expect(entries).toHaveLength(1)
    expect(entries[0]!.count).toBe(1)
    expect(entries[0]!.read).toBe(false)
  })

  it('the same mistake again counts up, a different one is another entry', async () => {
    await sleep(2200) // past the duplicate window
    log.addCommandError(await fail(() => board.services.device(1, 'IO', 'LEVEL', { pin: 98, level: 'Low' }, { side: 'write' })))
    await sleep(300)
    expect(log.entries({ ref: 'device:1' })[0]!.count).toBe(2)
    log.addCommandError(await fail(() => board.services.device(1, 'IO', 'LEVEL', { pin: 97, level: 'Low' }, { side: 'write' })))
    await sleep(300)
    expect(log.entries({ ref: 'device:1' })).toHaveLength(2)
  })

  it('read, unread and clear by device', async () => {
    expect(log.unread('device:1')).toBe(2)
    log.markRead({ ref: 'device:1' })
    expect(log.unread('device:1')).toBe(0)
    expect(log.clear({ ref: 'device:1' })).toBe(2)
  })

  it('the stream alone fills the log (an error nobody asked for is still shown)', async () => {
    log.clear()
    await board.link.request({ keys: [0xee] }).catch(() => undefined)
    expect(await until(() => log.entries().length > 0, 2000)).toBe(true)
  })

  it('a burst of 20 errors: every one answered, the log does not choke', async () => {
    log.clear()
    const results = await Promise.all(Array.from({ length: 20 }, (_, i) => board.services.device(1, 'IO', 'LEVEL', { pin: 100 + i, level: 'Low' }, { side: 'write' }).then(() => 'ok', () => 'refused')))
    expect(results.every((r) => r === 'refused')).toBe(true)
    await sleep(800)
    expect(log.entries().length).toBeGreaterThan(0)
    expect((await board.services.call('SYS', 'PING')).value).toBeDefined()
  })

  it('the console logged no panic', () => {
    expect(board.console.trouble(board.startedAt)).toEqual([])
  })
})
