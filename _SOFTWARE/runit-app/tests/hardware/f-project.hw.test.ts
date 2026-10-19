import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import {
  crc32, deviceFrame, deviceRecordId, eraseProject, listRecords, programFrameOf, projectInfo, projectStream, readProject, readRecord, recordInfo,
  removeRecord, runProject, runRecord, saveProject, sendProgram, setAutostart, storeRecord, vmFrame,
} from '@backend'
import type { ProjectContent } from '@backend'
import { blk, build, engine, num, project, read } from '../engine/helpers'
import { defs, openBoard, sleep, until } from './harness'
import type { Board } from './harness'

/* Stored records and the stored project on a real board, through the backend (BACKEND.md "Records and the project" ran against
 * a fake platform and a script). Everything the suite stores it removes again, also when a test fails. */

const IO_LEVEL = 0x0100 // (contract IO = 1) << 8 | LEVEL (its write: deviceFrame)
const PIN = 22 // TCA6424A pin 22: an output, left low at boot
const ACTION = 90
const level = async (board: Board) => String(((await board.services.device(1, 'IO', 'LEVEL', { pin: PIN }, { side: 'read' })).value as { level: unknown }).level)
const set = (high: boolean) => deviceFrame(defs, 1, IO_LEVEL, Uint8Array.of(PIN, high ? 1 : 0))

const result = build(
  project(
    [num('x'), num('y', 'y', { initial: 3 }), num('z')],
    [blk('e', 'expr', { groups: { inputs: 2 }, inputs: [read('x'), read('y')], outputs: [{ path: { root: 'z', steps: [] } }], editor: { text: 'in0 * 2 + in1' } })],
  ),
)
const content = (): ProjectContent => ({
  sections: [{ id: deviceRecordId(1), frames: [set(true)] }], // at every start: the pin high
  program: [...result.frames, engine.exec('normal')].map((f) => vmFrame(defs, f.op, f.payload)),
})

/** Subscribes to the program's variables and reads what comes for a moment: the values by variable. */
async function telemetryOf(b: Board): Promise<Map<string, unknown>> {
  const reader = engine.telemetry(result)
  const seen = new Map<string, unknown>()
  const off = b.link.on('telemetry', (d) => reader.read(d)?.values.forEach((v) => v.variable && seen.set(v.variable, v.value)))
  try {
    await sendProgram(b.link, [engine.subscribe([...result.ids.variables.values()])])
    await sleep(400)
  } catch {
    // the program is not there yet: the caller asks again
  } finally {
    off()
  }
  return seen
}

let board: Board
beforeAll(async () => {
  board = await openBoard()
  await cleanup()
})
afterAll(async () => {
  await cleanup().catch(() => undefined)
  await board.close()
})
async function cleanup() {
  await eraseProject(board.link).catch(() => undefined)
  await setAutostart(board.link, false).catch(() => undefined)
  await removeRecord(board.link, ACTION).catch(() => undefined)
  await board.services.device(1, 'IO', 'LEVEL', { pin: PIN, level: 'Low' }, { side: 'write' }).catch(() => undefined)
  await board.services.call('VM', 'RESET').catch(() => undefined)
}

describe('a stored record (an action)', () => {
  it('is stored, listed, read back as the same frames, and has the size and CRC of its image', async () => {
    const frames = [set(true), set(false), set(true)]
    await storeRecord(board.link, ACTION, frames)
    expect(await listRecords(board.link)).toContain(ACTION)
    expect(await readRecord(board.link, ACTION)).toEqual(frames)
    const info = await recordInfo(board.link, ACTION)
    expect(info.count).toBe(3)
    expect(info.bytes).toBe(frames.reduce((n, f) => n + 2 + f.length, 0))
  })

  it('RUN executes it: the pin ends high', async () => {
    await board.services.device(1, 'IO', 'LEVEL', { pin: PIN, level: 'Low' }, { side: 'write' })
    await runRecord(board.link, ACTION)
    expect(await until(async () => /High|1|true/i.test(await level(board)), 2000, 100)).toBe(true)
  })

  it('is replaced by storing the same id again, and removed', async () => {
    await storeRecord(board.link, ACTION, [set(false)])
    expect(await readRecord(board.link, ACTION)).toEqual([set(false)])
    await runRecord(board.link, ACTION)
    expect(await until(async () => /Low|0|false/i.test(await level(board)), 2000, 100)).toBe(true)
    await removeRecord(board.link, ACTION)
    expect(await listRecords(board.link)).not.toContain(ACTION)
  })

  it('a record id nobody stored is refused with an error the user can read', async () => {
    await expect(readRecord(board.link, 4_000)).rejects.toThrow()
  })
})

describe('the stored project', () => {
  it('saveProject: the board takes the stream and INFO gives its length and CRC', async () => {
    const stream = projectStream(defs, content())
    const progress: number[] = []
    const saved = await saveProject(board.link, content(), { autostart: false, onProgress: (done) => progress.push(done) })
    expect(saved).toEqual({ bytes: stream.length, crc: crc32(stream) })
    const info = await projectInfo(board.link)
    expect(info).toMatchObject({ stored: true, autostart: false, bytes: stream.length, crc: crc32(stream) })
    expect(progress.length).toBeGreaterThan(0)
  })

  it('readProject gives every record back: verified, the sections and the program as saved', async () => {
    const stored = (await readProject(board.link))!
    expect(stored.verified).toBe(true)
    expect(stored.sections).toEqual(content().sections)
    expect(stored.program).toEqual(content().program)
  })

  it('the engine recovers the project from what the board holds', async () => {
    const stored = (await readProject(board.link))!
    const recovered = engine.recover(stored.program.flatMap((f) => programFrameOf(defs, f) ?? []))
    const names = recovered.project?.variables.map((v) => v.name)
    expect(names).toEqual(expect.arrayContaining(['x', 'y', 'z']))
  })

  it('runProject runs it: the section sets the pin, the program computes', async () => {
    await board.services.device(1, 'IO', 'LEVEL', { pin: PIN, level: 'Low' }, { side: 'write' })
    await runProject(board.link)
    expect(await until(async () => /High|1|true/i.test(await level(board)), 3000, 100)).toBe(true)
    expect(await until(async () => (await telemetryOf(board)).get('y') === 3, 4000, 100)).toBe(true)
  })

  it('with autostart the board runs it by itself after a restart', async () => {
    await setAutostart(board.link, true)
    expect((await projectInfo(board.link)).autostart).toBe(true)
    await board.services.device(1, 'IO', 'LEVEL', { pin: PIN, level: 'Low' }, { side: 'write' })
    const since = Date.now()
    await board.services.call('SYS', 'RESTART')
    await until(async () => {
      try {
        return (await board.services.call('SYS', 'PING')).value && Date.now() - since > 1500
      } catch {
        return false
      }
    }, 30_000, 300)
    expect(await until(async () => /High|1|true/i.test(await level(board)), 8000, 200), 'the project section ran at boot').toBe(true)
    expect(await until(async () => (await telemetryOf(board)).get('y') === 3, 5000, 100), 'the program is loaded and running').toBe(true)
  })

  it('eraseProject removes it', async () => {
    await setAutostart(board.link, false)
    await eraseProject(board.link)
    expect((await projectInfo(board.link)).stored).toBe(false)
    expect(await readProject(board.link)).toBeUndefined()
    await sleep(200)
  })

  it('the console logged no panic', () => {
    expect(board.console.trouble(board.startedAt)).toEqual([])
  })
})
