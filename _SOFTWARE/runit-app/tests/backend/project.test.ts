import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { describe, expect, it } from 'vitest'
import {
  crc32, deviceFrame, deviceRecordId, frameImage, listRecords, parseImage, programFrameOf, projectInfo, projectStream, readProject, readRecord,
  projectBankRecordId, RunitLink, saveProject, SECTION_START, storeRecord, vmFrame,
} from '@backend'
import type { ProjectContent } from '@backend'
import { blk, build, engine, flag, num, project, read } from '../engine/helpers'
import { answer, defs, FakeBoard } from './fakes'

const IO_SET = 0x0100
const hex = (b: Uint8Array) => [...b].map((x) => x.toString(16).padStart(2, '0')).join('')
const bytes = (text: string) => Uint8Array.from(text.match(/../g)!.map((h) => Number.parseInt(h, 16)))

/* The project of the captured fixture (a runIT board stored it and gave every record back). */
const content = (): ProjectContent => {
  const result = build(
    project(
      [num('x'), num('y', 'y', { initial: 3 }), num('z'), flag('kill')],
      [
        blk('e', 'expr', { groups: { inputs: 2 }, inputs: [read('x'), read('y')], outputs: [{ path: { root: 'z', steps: [] } }], editor: { text: 'in0 * 2 + in1' } }),
        blk('a', 'action', { values: { action_id: 10 }, enables: [read('kill')] }),
        blk('t', 'io', { variant: 'toggle', device: 1, values: { pin: 22 }, enables: [read('kill')] }),
      ],
    ),
  )
  return {
    sections: [
      { id: deviceRecordId(1), frames: [deviceFrame(defs, 1, IO_SET, Uint8Array.of(22, 1)), deviceFrame(defs, 1, IO_SET, Uint8Array.of(23, 1))] },
      { id: 10, frames: [deviceFrame(defs, 1, IO_SET, Uint8Array.of(22, 0)), deviceFrame(defs, 1, IO_SET, Uint8Array.of(23, 0))] },
    ],
    program: [...result.frames, engine.exec('normal')].map((f) => vmFrame(defs, f.op, f.payload)),
  }
}

interface Fixture {
  stream: string
  crc: number
  bytes: number
  info: { stored: boolean; boot: number; bank: number; crc: number; bytes: number }
  records: Record<string, string[]>
}
const fixture = JSON.parse(readFileSync(fileURLToPath(new URL('./fixtures/stored-project.json', import.meta.url)), 'utf8')) as Fixture

describe('the stream', () => {
  it('crc32 is zlib / IEEE (the firmware checks it)', () => {
    expect(crc32(new TextEncoder().encode('123456789'))).toBe(0xcbf43926)
    expect(crc32(new Uint8Array(0))).toBe(0)
  })

  it('is the stream the board accepted: the same bytes, length and CRC (it stored it and reported them back)', () => {
    const stream = projectStream(defs, content())
    expect(hex(stream)).toBe(fixture.stream)
    expect([stream.length, crc32(stream)]).toEqual([fixture.bytes, fixture.crc])
    expect([fixture.info.bytes, fixture.info.crc]).toEqual([fixture.bytes, fixture.crc]) // the board's own INFO
  })

  it('an image is frames with their lengths, and reads back; a cut one does not', () => {
    const frames = [Uint8Array.of(1, 2, 3), Uint8Array.of(9)]
    expect(hex(frameImage(frames))).toBe('0300010203' + '010009')
    const image = frameImage(frames)
    expect(parseImage(image)).toEqual(frames)
    expect(parseImage(image.subarray(0, image.length - 1))).toBeUndefined()
    expect(() => frameImage([new Uint8Array(0)])).toThrow(RangeError)
  })

  it('the program is section 0, last; a section cannot be 0', () => {
    expect(() => projectStream(defs, { sections: [{ id: 0, frames: [Uint8Array.of(1)] }] })).toThrow(/program/)
    const frames = parseImage(projectStream(defs, content()))!
    const sections = frames.filter((f) => f[0] === defs.keys.project && f[1] === defs.dir.write && f[2] === defs.ops.project.SECTION)
    expect(sections.map((f) => (f[3]! | (f[4]! << 8) | (f[5]! << 16) | (f[6]! << 24)) >>> 0)).toEqual([0x02000001, 10, 0])
    expect(frames.at(-1)![0]).toBe(defs.keys.vm) // the last frame is the program's
  })

  it('the start section is 0xFFFFFFFF, after the program; a section cannot be it', () => {
    expect(() => projectStream(defs, { sections: [{ id: SECTION_START, frames: [Uint8Array.of(1)] }] })).toThrow(/start/)
    const exec = Uint8Array.of(defs.keys.vm!, defs.dir.write, 8, 0)
    const frames = parseImage(projectStream(defs, { ...content(), start: [exec] }))!
    const ids = frames.filter((f) => f[0] === defs.keys.project && f[2] === defs.ops.project.SECTION).map((f) => (f[3]! | (f[4]! << 8) | (f[5]! << 16) | (f[6]! << 24)) >>> 0)
    expect(ids).toEqual([0x02000001, 10, 0, SECTION_START])
    expect(frames.at(-1)).toEqual(exec)
    expect(projectBankRecordId(1, 0xffff)).toBe(0x0302ffff) // SYS_PROJECT_ID(bank 1, the start)
  })
})

describe('saving', () => {
  it('BEGIN with the length and the boot option, the stream in WRITEs at their offsets, COMMIT with the CRC', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!))
    const link = new RunitLink(board, { reconnect: false })
    await link.connect(board.device)
    const progress: number[] = []
    const result = await saveProject(link, content(), { boot: defs.projectBoot.load, window: 2, onProgress: (done) => progress.push(done) })
    const stream = projectStream(defs, content())
    expect(result).toEqual({ bytes: stream.length, crc: crc32(stream) })

    // [seq][flags][key][dir][op][args]: every command of a save is a write
    expect(board.requests.every((f) => f[3] === defs.dir.write)).toBe(true)
    const ops = board.requests.map((f) => f[4])
    const { BEGIN, WRITE, COMMIT } = defs.ops.project
    expect(ops[0]).toBe(BEGIN)
    expect(ops.at(-1)).toBe(COMMIT)
    const begin = board.requests[0]!
    expect([...begin.subarray(5, 9)]).toEqual([...new Uint8Array(new Uint32Array([stream.length]).buffer)])
    expect(begin[9]).toBe(defs.projectBoot.load)
    const writes = board.requests.filter((f) => f[4] === WRITE)
    const joined = new Uint8Array(stream.length)
    for (const w of writes) {
      const offset = new DataView(w.buffer, w.byteOffset).getUint32(5, true)
      joined.set(w.subarray(9), offset)
    }
    expect(joined).toEqual(stream)
    expect(writes.every((w) => w.length <= defs.frameMax)).toBe(true)
    expect(new DataView(board.requests.at(-1)!.buffer, board.requests.at(-1)!.byteOffset).getUint32(5, true)).toBe(crc32(stream))
    expect(progress.at(-1)).toBe(writes.length)
    await link.close()
  })

  it('stores a record the same way, with its frames as an image', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!))
    const link = new RunitLink(board, { reconnect: false })
    await link.connect(board.device)
    const frames = [deviceFrame(defs, 1, IO_SET, Uint8Array.of(22, 0)), deviceFrame(defs, 1, IO_SET, Uint8Array.of(23, 0))]
    await storeRecord(link, 10, frames)
    const [begin, write, commit] = board.requests
    expect(hex(begin!.subarray(5))).toBe('0a000000' + '1400')
    expect(hex(write!.subarray(5))).toBe('0000' + hex(frameImage(frames)))
    expect(hex(commit!.subarray(5))).toBe(hex(new Uint8Array(new Uint32Array([crc32(frameImage(frames))]).buffer)))
    await link.close()
  })
})

/** The board's record store, answering from the captured records. */
const store = (board: FakeBoard) => {
  const records = new Map(Object.entries(fixture.records).map(([id, frames]) => [Number(id), frames.map(bytes)]))
  const info = fixture.info
  const u32 = (n: number) => [n & 0xff, (n >>> 8) & 0xff, (n >>> 16) & 0xff, (n >>> 24) & 0xff]
  board.respond = (frame, send) => {
    const [seq, , key, dir, op] = frame
    const id = frame.length >= 9 ? (frame[5]! | (frame[6]! << 8) | (frame[7]! << 16) | (frame[8]! << 24)) >>> 0 : 0
    const read = dir === defs.dir.read
    if (!read && (op === defs.ops.project.INFO || op === defs.ops.actions.INFO || op === defs.ops.actions.READ || op === defs.ops.actions.LIST)) return send(answer(seq!, [], false))
    if (key === defs.keys.project && op === defs.ops.project.INFO) return send(answer(seq!, [info.stored ? 1 : 0, info.boot, info.bank, ...u32(info.crc), ...u32(info.bytes)]))
    if (key === defs.keys.actions && op === defs.ops.actions.INFO) {
      const frames = records.get(id)
      if (!frames) return send(answer(seq!, [], false))
      const image = frameImage(frames)
      return send(answer(seq!, [image.length & 0xff, image.length >> 8, frames.length & 0xff, frames.length >> 8, ...u32(crc32(image))]))
    }
    if (key === defs.keys.actions && op === defs.ops.actions.READ) {
      const frames = records.get(id)
      if (!frames) return send(answer(seq!, [], false))
      const offset = frame[9]! | (frame[10]! << 8)
      send(answer(seq!, [...frameImage(frames).subarray(offset, offset + 64)])) // a small page: the reader asks again from where it got to
    } else if (key === defs.keys.actions && op === defs.ops.actions.LIST) {
      const ids = [...records.keys()].sort((a, b) => a - b).filter((i) => i >= id)
      send(answer(seq!, [records.size & 0xff, records.size >> 8, ...ids.flatMap(u32)]))
    } else send(answer(seq!))
  }
}

describe('reading the project back from the board', () => {
  it('INFO, the records of the actions router, and the whole project: sections, then the program', async () => {
    const board = new FakeBoard()
    store(board)
    const link = new RunitLink(board, { reconnect: false })
    await link.connect(board.device)
    expect(await projectInfo(link)).toEqual(fixture.info)
    expect((await readRecord(link, 10)).map(hex)).toEqual(fixture.records['10'])
    expect(await listRecords(link)).toEqual(Object.keys(fixture.records).map(Number).sort((a, b) => a - b))

    const stored = (await readProject(link))!
    expect(stored.sections.map((s) => s.id)).toEqual([0x02000001, 10])
    expect(stored.sections[1]!.frames.map(hex)).toEqual(content().sections![1]!.frames.map(hex))
    expect(stored.program.map(hex)).toEqual(content().program!.map(hex))
    expect(stored.verified, 'the stream rebuilt from what was read has the CRC the board saved').toBe(true)
    await link.close()
  })

  it('and recovers the project from the program it read', async () => {
    const board = new FakeBoard()
    store(board)
    const link = new RunitLink(board, { reconnect: false })
    await link.connect(board.device)
    const stored = (await readProject(link))!
    const recovered = engine.recover(stored.program.flatMap((f) => programFrameOf(defs, f) ?? []))
    expect(recovered.exact).toBe(true)
    expect(recovered.parsed.exec).toEqual(['normal'])
    expect(recovered.project.variables.map((v) => v.name)).toEqual(['x', 'y', 'z', 'kill'])
    expect(recovered.project.blocks.map((b) => b.type)).toEqual(['expr', 'action', 'io'])
    expect(recovered.project.blocks[0]).toMatchObject({ editor: { text: 'in0 * 2 + in1' } })
    expect(recovered.project.blocks[1]).toMatchObject({ values: { action_id: 10 } })
    expect(recovered.project.blocks[2]).toMatchObject({ variant: 'toggle', device: 1, values: { pin: 22 } })
    await link.close()
  })

  it('no project stored is undefined', async () => {
    const board = new FakeBoard()
    board.respond = (frame, send) => send(answer(frame[0]!, [0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]))
    const link = new RunitLink(board, { reconnect: false })
    await link.connect(board.device)
    expect(await readProject(link)).toBeUndefined()
    await link.close()
  })
})
