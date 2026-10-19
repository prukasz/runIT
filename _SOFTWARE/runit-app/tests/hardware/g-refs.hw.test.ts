import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { CommandError, ErrorInterpreter, ErrorLog, LoadError, combineResolvers, deviceRefs, loadProgram, programRefs } from '@backend'
import type { ErrorReport } from '@backend'
import { blk, build, engine, num, project } from '../engine/helpers'
import { defs, openBoard, sleep } from './harness'
import type { Board } from './harness'

/* Errors a real board raises, tied back to what the user has: the names of the project's variables and blocks, the devices he
 * created, the pins and channels. Records captured from the board go through `ErrorInterpreter` with `programRefs` (the compile
 * result of the loaded program) and `deviceRefs`; what an error points at (`target`) is what the UI marks. Never an `obj-…` id. */

const DEVICES = new Map([[1, 'Expander'], [4, 'Analog out']])
const hasId = (text: string) => /\bobj-|\bvm_(object|accessor|block)\b/.test(text)

let board: Board
beforeAll(async () => {
  board = await openBoard()
  await board.services.call('VM', 'RESET')
})
afterAll(async () => {
  await board.services.call('VM', 'RESET').catch(() => undefined)
  await board.close()
})

const interpreterFor = (p: Parameters<typeof programRefs>[0], result: Parameters<typeof programRefs>[1]) =>
  new ErrorInterpreter({ resolve: combineResolvers(programRefs(p, result), deviceRefs(DEVICES)) })

/** The refs of a report as the UI would mark them. */
const targets = (interpreter: ErrorInterpreter, report: ErrorReport) => report.refs.map((r) => interpreter.target(r)).filter((t) => t !== undefined)
const everyText = (report: ErrorReport) => [report.message, ...report.where, report.command?.label ?? '', ...report.chain.flatMap((n) => [n.message, ...n.fields.map((f) => f.text)])]

/** Loads a program that the board refuses and gives the report of the refusal. */
async function refused(p: Parameters<typeof programRefs>[0], frames: { op: number; payload: Uint8Array }[], interpreter: ErrorInterpreter): Promise<ErrorReport> {
  const error = await loadProgram(board.link, [...frames, engine.exec('normal')], { retries: 0 }).then(
    () => undefined,
    (e: unknown) => e,
  )
  expect(error, 'the board loaded a program it should refuse').toBeInstanceOf(LoadError)
  expect((error as LoadError).reset).toBe('done')
  const record = ((error as LoadError).cause as CommandError).record
  expect(record).toBeDefined()
  void p
  return interpreter.interpret(record!)
}

describe('a program the board refuses at load: the error names the user\'s block and variable', () => {
  const p = project(
    [num('x', 'Speed'), num('y', 'Offset', { initial: 3 }), num('z', 'Result')],
    [blk('e', 'expr', { name: 'Compute', groups: { inputs: 2 }, inputs: [{ kind: 'variable', path: { root: 'x', steps: [] } }, { kind: 'variable', path: { root: 'y', steps: [] } }], outputs: [{ path: { root: 'z', steps: [] } }], editor: { text: 'in0 * 2 + in1' } })],
  )
  const result = build(p)
  const interpreter = interpreterFor(p, result)

  it('objects that were never added: "input 2 of Compute does not exist", marked on that block input', async () => {
    const report = await refused(p, [result.frames[0]!, ...result.frames.slice(2)], interpreter)
    expect(report.message).toBe('input 2 of Compute does not exist')
    expect(targets(interpreter, report)).toContainEqual({ kind: 'block', block: 'e', pin: { kind: 'input', index: 1 } })
    expect(everyText(report).filter(hasId)).toEqual([])
  })

  it('the error log keeps it under the block, and the board still takes a good program after it', async () => {
    const log = new ErrorLog(interpreter)
    const error = await loadProgram(board.link, [...result.frames.slice(0, 1), ...result.frames.slice(2)], { retries: 0 }).then(() => undefined, (e: unknown) => e)
    log.addCommandError((error as LoadError).cause)
    expect(log.entries().length).toBe(1)
    log.close()
    await loadProgram(board.link, [...result.frames, engine.exec('normal')])
  })
})

describe('a block whose output variable does not exist on the board', () => {
  // no inputs, so the first thing the board cannot find is the output object
  const p = project([num('z', 'Result')], [blk('c', 'expr', { name: 'Constant', groups: { inputs: 0 }, outputs: [{ path: { root: 'z', steps: [] } }], editor: { text: '42' } })])
  const result = build(p)
  const interpreter = interpreterFor(p, result)

  it('is refused with the variable\'s name and the block\'s, marked on both', async () => {
    const report = await refused(p, [result.frames[0]!, ...result.frames.slice(2)], interpreter)
    const all = everyText(report).join(' | ')
    expect(report.message).toBe('output 1 of Constant refers to Result, which does not exist')
    expect(all).toMatch(/Constant/)
    const marks = targets(interpreter, report)
    expect(marks).toContainEqual({ kind: 'block', block: 'c', pin: { kind: 'output', index: 0 } }) // the output the user wired to Result
    expect(everyText(report).filter(hasId)).toEqual([])
  })
})

describe('a block that fails while the program runs: the error comes on the errors stream', () => {
  const run = async (device: number, pin: number) => {
    const p = project([num('x', 'Speed')], [blk('t', 'io', { name: 'Lamp', variant: 'toggle', device, values: { pin } })])
    const result = build(p)
    const interpreter = interpreterFor(p, result)
    const seen: Uint8Array[] = []
    const off = board.link.on('errors', (d) => seen.push(d))
    await loadProgram(board.link, [...result.frames, engine.exec('normal')])
    await sleep(1200)
    off()
    await board.services.call('VM', 'RESET')
    const reports = seen.map((d) => interpreter.read(d)).filter((r): r is ErrorReport => r !== undefined)
    return { interpreter, reports }
  }

  it('a pin the device does not have: "Expander has no Pin 99", on the block, the device and the pin', async () => {
    const { interpreter, reports } = await run(1, 99)
    const report = reports.find((r) => r.chain.some((n) => n.name === 'ERR_DEV_BAD_PIN'))
    expect(report, `records: ${reports.map((r) => r.message).join(' / ')}`).toBeDefined()
    expect(report!.message).toBe('Expander has no Pin 99')
    expect(report!.where).toContain('In Expander')
    const marks = targets(interpreter, report!)
    expect(marks).toContainEqual({ kind: 'block', block: 't' })
    expect(marks).toContainEqual({ kind: 'device', device: 1 })
    expect(marks).toContainEqual({ kind: 'pin', device: 1, pin: 99 })
    expect(report!.chain[0]!.fields.map((f) => f.text)).toContain('Lamp') // the block's name, not its number
    expect(reports.flatMap(everyText).filter(hasId)).toEqual([])
  })

  it('a device that does not exist: named by its number, the block still by its name', async () => {
    const { interpreter, reports } = await run(77, 1)
    const report = reports.find((r) => r.chain.some((n) => n.name === 'ERR_DEV_NOT_FOUND'))
    expect(report, `records: ${reports.map((r) => r.message).join(' / ')}`).toBeDefined()
    expect(report!.message).toBe('Device 77 does not exist')
    expect(targets(interpreter, report!)).toContainEqual({ kind: 'block', block: 't' })
    expect(report!.chain[0]!.fields.map((f) => f.text)).toContain('Lamp')
  })

  it('the board raises the same block error again each time, and the log counts it under the block', async () => {
    const { interpreter, reports } = await run(1, 99)
    const log = new ErrorLog(interpreter)
    for (const r of reports) log.add(r)
    const entry = log.entries().find((e) => e.report.message === 'Expander has no Pin 99')
    expect(entry).toBeDefined()
    expect(log.entries({ ref: 'device:1' }).length).toBeGreaterThan(0)
    expect(log.entries({ ref: 'device:1/pin:99' }).length).toBeGreaterThan(0)
    log.close()
  })
})

describe('a command to a device', () => {
  const interpreter = new ErrorInterpreter({ resolve: deviceRefs(DEVICES) })
  const fail = async (run: () => Promise<unknown>): Promise<ErrorReport> => {
    const error = await run().then(() => undefined, (e: unknown) => e)
    expect(error).toBeInstanceOf(CommandError)
    return interpreter.interpret((error as CommandError).record!)
  }

  it('a pin: the device by its name, the pin by its number, both marked', async () => {
    const report = await fail(() => board.services.device(1, 'IO', 'LEVEL', { pin: 99, level: 'High' }, { side: 'write' }))
    expect(report.message).toBe('Expander has no Pin 99')
    expect(targets(interpreter, report)).toEqual(expect.arrayContaining([{ kind: 'device', device: 1 }, { kind: 'pin', device: 1, pin: 99 }]))
  })

  it('a channel of the DAC: marked as a channel of that device, not as a pin', async () => {
    const report = await fail(() => board.services.device(4, 'IO', 'VOLTAGE', { pin: 5, voltage_mV: 100 }, { side: 'write' }))
    const marks = targets(interpreter, report) as { kind: string; device?: number; channel?: number; pin?: number }[]
    expect(marks.some((t) => t.device === 4 && (t.channel === 5 || t.pin === 5)), JSON.stringify(marks)).toBe(true)
    expect(report.message).toMatch(/Analog out/)
  })

  it('a device command that does not exist is a command of that device', async () => {
    const report = await fail(() => board.link.request({ keys: [defs.keys.device!, defs.dir.write, 1, 0, 0xff, 0x7f] }))
    expect(report.refs.some((r) => r.kind === 'device' && r.id === 1)).toBe(true)
    expect(report.chain.some((n) => n.name === 'ERR_DEV_UNKNOWN_OP')).toBe(true)
  })

  it('a write to a variable beyond its size: refused, and the variable is named', async () => {
    const p = project([num('x', 'Speed')], [])
    const result = build(p)
    await loadProgram(board.link, [...result.frames, engine.exec('normal')])
    const object = result.ids.variables.get('x')!
    const programInterpreter = interpreterFor(p, result)
    const payload = Uint8Array.of(1, object & 0xff, object >> 8, 9, 0, 4, 0, 0, 0, 0, 0) // one record: object, element 9 of 1, 4 bytes
    const error = await board.link.request({ keys: [defs.keys.vm!, defs.dir.write, engine.wire.ops.setData], payload }).then(() => undefined, (e: unknown) => e)
    expect(error, 'the board took a write beyond the variable').toBeInstanceOf(CommandError)
    const report = programInterpreter.interpret((error as CommandError).record!)
    const all = everyText(report).join(' | ')
    expect(all, all).toMatch(/Speed/)
    expect(targets(programInterpreter, report)).toContainEqual({ kind: 'variable', variable: 'x' })
    expect(everyText(report).filter(hasId)).toEqual([])
    await board.services.call('VM', 'RESET')
  })

  it('the console logged no panic', () => {
    expect(board.console.trouble(board.startedAt)).toEqual([])
  })
})
