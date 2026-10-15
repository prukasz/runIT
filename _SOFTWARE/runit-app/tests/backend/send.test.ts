import { describe, expect, it } from 'vitest'
import { RunitLink, sendInOrder, sendProgram } from '@backend'
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
