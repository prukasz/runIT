import { describe, expect, it } from 'vitest'
import { bundledServiceCatalog, FLASHER_STATE, FlasherFailed, flashAttached, RunitLink, writeFirmware } from '@backend'
import { answer, defs, FakeBoard } from './fakes'

const catalog = bundledServiceCatalog()

// sys_flasher_status_reply_t: state u8, step u8, done u32, total u32, error u32 (little-endian).
const status = (state: number, step: number, done: number, total: number, error = 0): number[] => {
  const out = new Uint8Array(14)
  const view = new DataView(out.buffer)
  out[0] = state
  out[1] = step
  view.setUint32(2, done, true)
  view.setUint32(6, total, true)
  view.setUint32(10, error, true)
  return [...out]
}

const connected = async (respond: FakeBoard['respond']) => {
  const board = new FakeBoard()
  board.respond = respond
  const link = new RunitLink(board, { timeoutMs: 200, reconnect: false })
  await link.connect(board.device)
  return { board, link }
}

// A request as a test sees it: [seq][flags][key][dir][op]...
const flasherOp = (frame: Uint8Array): number | undefined => (frame[2] === defs.keys.flasher ? frame[4] : undefined)

describe('flashing the board on the link', () => {
  it('starts it and follows STATUS until DONE, telling each status', async () => {
    const reads = [status(FLASHER_STATE.running, 3, 4096, 8192), status(FLASHER_STATE.done, 4, 8192, 8192)]
    const ops: number[] = []
    const { link } = await connected((frame, send) => {
      const op = flasherOp(frame)
      if (op !== undefined) ops.push(op)
      send(answer(frame[0]!, op === 2 ? reads.shift() : []))
    })
    const seen: number[] = []
    const last = await flashAttached(link, catalog, { pollMs: 1, onProgress: (s) => seen.push(s.done) })
    expect(ops).toEqual([1, 2, 2])
    expect(seen).toEqual([4096, 8192])
    expect(last).toMatchObject({ state: FLASHER_STATE.done, step: 4, done: 8192, total: 8192, error: 0 })
  })

  it('throws FlasherFailed with the status when it ends FAILED', async () => {
    const { link } = await connected((frame, send) =>
      send(answer(frame[0]!, flasherOp(frame) === 2 ? status(FLASHER_STATE.failed, 0, 0, 8192, 0x00190003) : [])),
    )
    const failure = await flashAttached(link, catalog, { pollMs: 1 }).catch((e: unknown) => e)
    expect(failure).toBeInstanceOf(FlasherFailed)
    expect((failure as FlasherFailed).status).toMatchObject({ state: FLASHER_STATE.failed, step: 0, error: 0x00190003 })
  })
})

describe('the firmware update of a slave', () => {
  it('sends every frame of it to that board, through the one the link is on', async () => {
    const { board, link } = await connected((frame, send) => send(answer(frame[0]!)))
    const before = board.addresses.length
    await writeFirmware(link, catalog, new Uint8Array(1200).fill(7), { board: 2 })
    const sent = board.addresses.slice(before)
    expect(sent.length).toBeGreaterThan(3) // VM RESET, BEGIN, the writes, COMMIT
    expect(sent.every((a) => a.dst === 2)).toBe(true)
  })
})
