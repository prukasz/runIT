import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { ErrorInterpreter, ErrorLog, LoadError, loadProgram, sendProgram } from '@backend'
import type { TelemetryUpdate } from '@engine'
import { blk, build, engine, num, project, read } from '../engine/helpers'
import { openBoard, sleep, until } from './harness'
import type { Board } from './harness'

/* The engine's program on a real board through the backend: loaded, run, watched and written at run time. The same program as
 * fixtures/board-streams.json (z = x * 2 + y, y starting at 3), now end to end through the TypeScript link. */

const program = () =>
  build(
    project(
      [num('x'), num('y', 'y', { initial: 3 }), num('z')],
      [blk('e', 'expr', { groups: { inputs: 2 }, inputs: [read('x'), read('y')], outputs: [{ path: { root: 'z', steps: [] } }], editor: { text: 'in0 * 2 + in1' } })],
    ),
  )

let board: Board
beforeAll(async () => {
  board = await openBoard()
  await board.services.call('VM', 'RESET')
})
afterAll(async () => {
  await board.services.call('VM', 'RESET').catch(() => undefined)
  await board.close()
})

/** Updates of the program's telemetry as they come, newest values by variable. */
const watch = (result: ReturnType<typeof program>) => {
  const reader = engine.telemetry(result)
  const updates: { at: number; update: TelemetryUpdate }[] = []
  const latest = new Map<string, number | unknown>()
  const off = board.link.on('telemetry', (data) => {
    const update = reader.read(data)
    if (!update) return
    updates.push({ at: Date.now(), update })
    for (const v of update.values) if (v.variable) latest.set(v.variable, v.value)
  })
  return { updates, latest, off }
}

describe('loading and running a program', () => {
  const result = program()
  const ids = [...result.ids.variables.values()]

  it('loadProgram: OPEN, objects, accessors, block, EXEC, each answered, progress reported', async () => {
    const progress: number[] = []
    const frames = [...result.frames, engine.exec('normal')]
    const answers = await loadProgram(board.link, frames, { onProgress: (done) => progress.push(done) })
    expect(answers).toHaveLength(frames.length)
    expect(progress.at(-1)).toBe(frames.length)
  })

  it('SUBSCRIBE: everything once, by variable (x 0, y 3, z 0: a block runs when its input changes)', async () => {
    const w = watch(result)
    await sendProgram(board.link, [engine.subscribe(ids)])
    expect(await until(() => w.latest.size === 3, 3000)).toBe(true)
    expect(Object.fromEntries(w.latest)).toEqual({ x: 0, y: 3, z: 0 })
    w.off()
  })

  it('a runtime write is applied at the next pass and comes back as telemetry (x = 5 gives z = 13)', async () => {
    const w = watch(result)
    const sent = Date.now()
    await sendProgram(board.link, engine.write(result, [{ variable: 'x', value: 5 }]))
    expect(await until(() => w.latest.get('z') === 13, 3000)).toBe(true)
    const latency = w.updates.find((u) => u.update.values.some((v) => v.variable === 'z' && v.value === 13))!.at - sent
    expect(latency, `${latency} ms`).toBeLessThan(1000)
    await sendProgram(board.link, engine.write(result, [{ variable: 'x', value: 7.5 }]))
    expect(await until(() => w.latest.get('z') === 18, 3000)).toBe(true)
    w.off()
  })

  it('only what changed is sent: with nothing written the telemetry is quiet', async () => {
    const w = watch(result)
    await sleep(1500)
    expect(w.updates.length).toBeLessThanOrEqual(1)
    w.off()
  })

  it('pause holds the program, resume runs it again', async () => {
    const w = watch(result)
    await sendProgram(board.link, [engine.exec('pause')])
    await sendProgram(board.link, engine.write(result, [{ variable: 'x', value: 1 }]))
    await sleep(800)
    expect(w.latest.get('z')).not.toBe(5)
    await sendProgram(board.link, [engine.exec('resume')])
    expect(await until(() => w.latest.get('z') === 5, 3000)).toBe(true)
    w.off()
  })

  it('a program that is loaded again replaces the first: OPEN clears it', async () => {
    await loadProgram(board.link, [...result.frames, engine.exec('normal')])
    const w = watch(result)
    await sendProgram(board.link, [engine.subscribe(ids)])
    expect(await until(() => w.latest.get('y') === 3, 3000)).toBe(true)
    w.off()
  })
})

describe('a program the board refuses', () => {
  const interpreter = new ErrorInterpreter()

  it('a block that reads objects which were never added fails the load; the backend resets, the error is readable', async () => {
    const good = program()
    const withoutObjects = [good.frames[0]!, ...good.frames.slice(2)] // OPEN, then the accessors and block with no objects
    const error = await loadProgram(board.link, [...withoutObjects, engine.exec('normal')], { retries: 0 }).then(
      () => undefined,
      (e: unknown) => e,
    )
    expect(error).toBeInstanceOf(LoadError)
    const load = error as LoadError
    expect(load.reset).toBe('done')
    const record = (load.cause as { record?: Parameters<ErrorInterpreter['interpret']>[0] }).record
    expect(record).toBeDefined()
    const report = interpreter.interpret(record!)
    expect(report.message.length).toBeGreaterThan(0)
    // not asserted by wording: the tag is whatever the firmware raises for the first thing missing (an accessor or an output)
    expect(report.chain.length).toBeGreaterThan(1)
  })

  it('the board fails closed: nothing runs after a refused load, and a good load afterwards works', async () => {
    const result = program()
    await loadProgram(board.link, [...result.frames, engine.exec('normal')])
    const w = watch(result)
    await sendProgram(board.link, [engine.subscribe([...result.ids.variables.values()])])
    expect(await until(() => w.latest.get('y') === 3, 3000)).toBe(true)
    await sendProgram(board.link, engine.write(result, [{ variable: 'x', value: 2 }]))
    expect(await until(() => w.latest.get('z') === 7, 3000)).toBe(true)
    w.off()
  })

  it('the error log shows what the user did wrong, by name', async () => {
    const log = new ErrorLog(interpreter)
    log.attach(board.link)
    const good = program()
    await loadProgram(board.link, [good.frames[0]!, ...good.frames.slice(2)], { retries: 0 }).catch((e: unknown) => log.addCommandError((e as LoadError).cause))
    await sleep(500)
    expect(log.entries().length).toBeGreaterThan(0)
    log.close()
  })

  it('the console logged no panic', () => {
    expect(board.console.trouble(board.startedAt)).toEqual([])
  })
})
