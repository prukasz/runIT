import { describe, expect, it } from 'vitest'
import { crc32, projectSourceName, readProjectSource, RunitLink, saveProjectSource } from '@backend'
import { decodeProjectSource, encodeProjectSource } from '../../src/backend/session/projectSource'
import { answer, defs, FakeBoard } from './fakes'

const source = { name: 'Łukasz arm', text: JSON.stringify({ blocks: Array.from({ length: 200 }, (_, i) => ({ id: `b${i}`, type: 'set' })) }), streamCrc: 0xdeadbeef }

/** The board's file: chunks as they came, its length and CRC. */
const withFile = (board: FakeBoard) => {
  const chunks: Uint8Array[] = []
  const whole = () => Uint8Array.from(chunks.flatMap((c) => [...c]))
  board.respond = (frame, send) => {
    const [seq, , key, , op] = frame
    const payload = frame.subarray(5)
    if (key !== defs.keys.project) return send(answer(seq!))
    if (op === defs.ops.project.SOURCE_ERASE) chunks.length = 0
    else if (op === defs.ops.project.SOURCE_APPEND) chunks.push(payload.slice(4))
    else if (op === defs.ops.project.SOURCE_INFO) {
      const out = new DataView(new ArrayBuffer(10))
      out.setUint32(0, whole().length, true)
      out.setUint32(4, crc32(whole()), true)
      out.setUint16(8, chunks.length, true)
      return send(answer(seq!, [...new Uint8Array(out.buffer)]))
    } else if (op === defs.ops.project.SOURCE_READ) return send(answer(seq!, [...chunks[payload[0]! | (payload[1]! << 8)]!]))
    send(answer(seq!))
  }
  return chunks
}

describe('the project\'s source on the board', () => {
  it('comes back as it went in, the head in front of the gzip', async () => {
    const bytes = await encodeProjectSource(source)
    expect(bytes[0]).toBe(1)
    expect(new DataView(bytes.buffer).getUint32(1, true)).toBe(source.streamCrc)
    expect(bytes.length).toBeLessThan(source.text.length / 2)
    expect(await decodeProjectSource(bytes)).toEqual(source)
  })

  it('refuses a format it does not know', async () => {
    const bytes = await encodeProjectSource(source)
    bytes[0] = 99
    await expect(decodeProjectSource(bytes)).rejects.toThrow(/format/)
  })

  it('is written in chunks and read back, the name from the first alone', async () => {
    const board = new FakeBoard()
    const chunks = withFile(board)
    const link = new RunitLink(board, { reconnect: false })
    await link.connect(board.device)
    expect(await readProjectSource(link)).toBeUndefined()
    expect(await projectSourceName(link)).toBeUndefined()
    await saveProjectSource(link, { ...source, text: source.text.repeat(20) })
    expect(chunks.every((c) => c.length <= 400)).toBe(true)
    expect(await projectSourceName(link)).toBe(source.name)
    expect(await readProjectSource(link)).toEqual({ ...source, text: source.text.repeat(20) })
    await link.close()
  })

  it('a chunk lost on the way is an error, not a project', async () => {
    const board = new FakeBoard()
    const chunks = withFile(board)
    const link = new RunitLink(board, { reconnect: false })
    await link.connect(board.device)
    await saveProjectSource(link, source)
    const respond = board.respond
    board.respond = (frame, send) => (frame[4] === defs.ops.project.SOURCE_READ ? send(answer(frame[0]!, [1, 2, 3])) : respond(frame, send))
    await expect(readProjectSource(link)).rejects.toThrow(/whole/)
    expect(chunks.length).toBeGreaterThan(0)
    await link.close()
  })
})
