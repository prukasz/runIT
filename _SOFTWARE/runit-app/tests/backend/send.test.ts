import { describe, expect, it } from 'vitest'
import { bundledServiceCatalog, CommandError, LOAD_COMMANDS, LoadError, loadProgram, RunitLink, sendInOrder, sendProgram } from '@backend'
import { blk, build, engine, num, project, read } from '../engine/helpers'
import { answer, defs, FakeBoard, until } from './fakes'

const linkTo = async (board: FakeBoard) => {
  const link = new RunitLink(board, { timeoutMs: 200, reconnect: false })
  await link.connect(board.device)
  return link
}

const req = (i: number) => ({ keys: [defs.keys.sys!, 0x01], payload: Uint8Array.of(i) })

describe('sending a list in order', () => {
  it('sends in order, answers each, and reports progress', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!, [frame[4]!])) // echoes the payload byte
    const link = await linkTo(board)
    const progress: [number, number][] = []
    const answers = await sendInOrder(link, [0, 1, 2, 3, 4, 5, 6, 7, 8, 9].map(req), { onProgress: (d, t) => progress.push([d, t]) })
    expect(answers.map((a) => a[0])).toEqual([0, 1, 2, 3, 4, 5, 6, 7, 8, 9])
    expect(board.requests.map((f) => f[4])).toEqual([0, 1, 2, 3, 4, 5, 6, 7, 8, 9]) // the board saw them in order
    expect(progress.at(-1)).toEqual([10, 10])
    expect(progress.map((p) => p[0])).toEqual([1, 2, 3, 4, 5, 6, 7, 8, 9, 10])
    await link.close()
  })

  it('keeps no more than the window on the wire', async () => {
    const board = new FakeBoard()
    let open = 0
    let most = 0
    board.respond = (frame, send) => {
      most = Math.max(most, ++open)
      setTimeout(() => {
        open--
        send(answer(frame[0]!))
      }, 5)
    }
    const link = await linkTo(board)
    await sendInOrder(link, Array.from({ length: 12 }, (_, i) => req(i)), { window: 3 })
    expect(most).toBe(3)
    await sendInOrder(link, Array.from({ length: 6 }, (_, i) => req(i)), { window: 1 })
    expect(most).toBe(3) // one at a time did not raise it
    await link.close()
  })

  it('stops at the first failure: nothing after it is sent, and the error is the link\'s own', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!, [], frame[4] !== 2)) // request 2 is refused
    const link = await linkTo(board)
    const result = await sendInOrder(link, Array.from({ length: 20 }, (_, i) => req(i)), { window: 2 }).catch((e: unknown) => e)
    expect(result).toMatchObject({ code: 'device-error' })
    expect(board.requests.length).toBeLessThanOrEqual(4) // 0, 1, 2 and at most the one already on the wire
    expect(board.requests.map((f) => f[4])).not.toContain(10)
    await link.close()
  })

  it('an empty list is nothing to send', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    expect(await sendInOrder(link, [])).toEqual([])
    expect(board.requests).toHaveLength(0)
    await link.close()
  })
})

describe('sending a program', () => {
  it('sends the frames the engine compiled to the VM router, each answered', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!))
    const link = await linkTo(board)
    const result = build(project([num('x'), num('y')], [blk('e', 'expr', { groups: { inputs: 2 }, inputs: [read('x'), read('y')], editor: { text: 'in0 * 2 + in1' } })]))
    expect(result.frames.length).toBeGreaterThan(2)
    const answers = await sendProgram(link, result.frames)
    expect(answers).toHaveLength(result.frames.length)
    await until(() => board.requests.length === result.frames.length)
    result.frames.forEach((frame, i) => {
      const sent = board.requests[i]!
      expect([sent[2], sent[3]]).toEqual([engine.wire.routerKey, frame.op]) // [seq][flags][vm key][op]
      expect([...sent.subarray(4)]).toEqual([...frame.payload])
    })
    expect(defs.keys.vm).toBe(engine.wire.routerKey) // the engine and the backend agree on the VM router's key
    await link.close()
  })
})

describe('loading a program', () => {
  const vm = bundledServiceCatalog().service(LOAD_COMMANDS.service)
  const OPEN = vm.commands.get(LOAD_COMMANDS.open)!.op
  const RESET = vm.commands.get(LOAD_COMMANDS.reset)!.op
  const result = build(project([num('x'), num('y')], [blk('e', 'expr', { groups: { inputs: 2 }, inputs: [read('x'), read('y')], editor: { text: 'in0 * 2 + in1' } })]))
  const ops = (board: FakeBoard) => board.requests.map((f) => f[3])

  it("names the commands the descriptor has, and the engine's load starts with its OPEN", () => {
    expect(vm.key).toBe(defs.keys.vm)
    expect(vm.commands.get(LOAD_COMMANDS.reset)!.args).toBeUndefined() // RESET has no payload
    expect(result.frames[0]!.op).toBe(OPEN)
  })

  it('loads a program like sendProgram when nothing fails', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!))
    const link = await linkTo(board)
    expect(await loadProgram(link, result.frames)).toHaveLength(result.frames.length)
    expect(ops(board)).toEqual(result.frames.map((f) => f.op))
    await link.close()
  })

  it('refuses a list that does not start with OPEN, sending nothing', async () => {
    const board = new FakeBoard()
    const link = await linkTo(board)
    await expect(loadProgram(link, result.frames.slice(1))).rejects.toThrow(/starts with VM OPEN/)
    expect(board.requests).toHaveLength(0)
    await link.close()
  })

  it('sends the whole load again from OPEN when the link drops in the middle', async () => {
    const board = new FakeBoard()
    let dropped = false
    board.respond = (frame, send) => {
      if (!dropped && board.requests.length === 3) {
        dropped = true
        return board.drop() // the third frame is on the wire: it may have run
      }
      send(answer(frame[0]!))
    }
    const link = new RunitLink(board, { timeoutMs: 200 })
    await link.connect(board.device)
    const retries: [number, string][] = []
    const answers = await loadProgram(link, result.frames, { window: 1, onRetry: (attempt, e) => retries.push([attempt, (e as CommandError).code]) })
    expect(answers).toHaveLength(result.frames.length)
    expect(retries).toEqual([[2, 'link-lost']])
    const sent = ops(board)
    expect(sent.filter((op) => op === OPEN)).toHaveLength(2)
    expect(sent.slice(3)).toEqual(result.frames.map((f) => f.op)) // the second attempt is the whole load, from OPEN
    expect(sent).not.toContain(RESET)
    await link.close()
  })

  it("does not retry what the board refuses: RESET, then LoadError with the board's error", async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!, [], frame[3] !== result.frames[2]!.op || frame[3] === RESET))
    const link = await linkTo(board)
    const retried: number[] = []
    const error = (await loadProgram(link, result.frames, { window: 1, onRetry: (a) => retried.push(a) }).catch((e: unknown) => e)) as LoadError
    expect(error).toBeInstanceOf(LoadError)
    expect(error).toMatchObject({ attempts: 1, answered: 2, reset: 'done' })
    expect((error.cause as CommandError).code).toBe('device-error')
    expect(retried).toEqual([])
    expect(ops(board).at(-1)).toBe(RESET)
    expect(ops(board).filter((op) => op === OPEN)).toHaveLength(1)
    await link.close()
  })

  it('gives up after the retries: each attempt from OPEN, then RESET; reset: false leaves the board as it is', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => (frame[3] === OPEN ? undefined : send(answer(frame[0]!))) // OPEN never answered
    const link = new RunitLink(board, { timeoutMs: 30, reconnect: false, stallAfter: 100 })
    await link.connect(board.device)
    const error = (await loadProgram(link, result.frames, { window: 1, retries: 1 }).catch((e: unknown) => e)) as LoadError
    expect(error).toMatchObject({ attempts: 2, answered: 0, reset: 'done' })
    expect((error.cause as CommandError).code).toBe('timeout')
    expect(ops(board)).toEqual([OPEN, OPEN, RESET])

    board.requests.length = 0
    const skipped = (await loadProgram(link, result.frames, { window: 1, retries: 0, reset: false }).catch((e: unknown) => e)) as LoadError
    expect(skipped).toMatchObject({ attempts: 1, reset: 'skipped' })
    expect(ops(board)).toEqual([OPEN])
    await link.close()
  })

  it('says when RESET did not get through either', async () => {
    const board = new FakeBoard()
    board.respond = () => undefined // nothing is answered
    const link = new RunitLink(board, { timeoutMs: 30, reconnect: false, stallAfter: 100 })
    await link.connect(board.device)
    const error = (await loadProgram(link, result.frames, { retries: 0 }).catch((e: unknown) => e)) as LoadError
    expect(error).toMatchObject({ attempts: 1, reset: 'failed' })
    expect((error.resetError as CommandError).code).toBe('timeout')
    await link.close()
  })
})
