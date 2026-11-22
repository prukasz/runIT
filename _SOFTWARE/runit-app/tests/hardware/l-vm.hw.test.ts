import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import type { LoadFrame } from '@engine'
import { defs, openBoard, until } from './harness'
import type { Board } from './harness'
import { blk, build, engine, num, project, read } from '../engine/helpers'

/* The VM on the board (VM_CORE.MD): a program loaded over the session and its values heard on the stream of MODULE_ID_VM. */

let board: Board
beforeAll(async () => {
  board = await openBoard()
})
afterAll(async () => {
  await send(engine.reset()).catch(() => undefined)
  await board.close()
})

/** One command of the VM router as a write: the session's header is the link's. */
const send = (frame: LoadFrame) => board.link.request({ key: defs.keys.vm!, body: Uint8Array.of(defs.dir.write, frame.op, ...frame.payload) })

describe('the VM', () => {
  it('answers its status', async () => {
    const { value } = await board.services.call('VM', 'STATUS')
    expect(value).toBeDefined()
  })

  it('runs a program and sends the value of a variable out of its stream', async () => {
    // b = 2.5 (a constant of the block): the first pass changes b, so the board sends it
    const program = build(project([num('a'), num('b')], [blk('s', 'set', { inputs: [null, read('b')], values: { value: 2.5 } })]))
    const reader = engine.telemetry(program)
    const heard: { variable?: string; value: unknown }[] = []
    const off = board.link.on('vm', (data) => {
      for (const v of reader.read(data)?.values ?? []) heard.push({ variable: v.variable, value: v.value })
    })
    await send(engine.reset())
    for (const frame of program.frames) await send(frame)
    await send(engine.subscribe([...program.ids.variables.values()]))
    await send(engine.exec('normal'))
    const got = await until(() => heard.find((v) => v.variable === 'b' && v.value === 2.5), 5000)
    off()
    expect(heard.some((v) => v.variable === 'b')).toBe(true) // the subscribe sends what is there first
    expect(got).toBeDefined()
  })
})
