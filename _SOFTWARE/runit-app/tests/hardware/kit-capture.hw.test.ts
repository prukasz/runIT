import { writeFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { LoadError, loadProgram } from '@backend'
import { engine } from '../engine/helpers'
import { compiled } from '../../design/sample'
import { openBoard, sleep } from './harness'
import type { Board } from './harness'

/* Captures the error records the UI kit shows, from the board (`KIT_CAPTURE=1 npm run test:hw`; skipped otherwise): the raw bytes of the
 * errors stream, as the backend receives them. design/build-kit.mjs reads them with the interpreter, so the page shows what the backend
 * says about what the board sent. */

const OUT = fileURLToPath(new URL('../../design/board-errors.json', import.meta.url))
const hex = (d: Uint8Array) => Buffer.from(d).toString('hex')

let board: Board
beforeAll(async () => {
  board = await openBoard()
  await board.services.call('VM', 'RESET')
})
afterAll(async () => {
  await board.services.call('VM', 'RESET').catch(() => undefined)
  await board.close()
})

describe.runIf(process.env.KIT_CAPTURE)('capture', () => {
  it('records the error scenarios', async () => {
    const samples: { id: string; title: string; program: 'compute' | 'lamp' | null; hex: string }[] = []
    const capture = async (id: string, title: string, program: 'compute' | 'lamp' | null, run: () => Promise<unknown>) => {
      const seen: Uint8Array[] = []
      const off = board.link.on('errors', (d) => seen.push(d))
      await run().catch(() => undefined)
      await sleep(900)
      off()
      expect(seen.length, `${id}: no record`).toBeGreaterThan(0)
      samples.push({ id, title, program, hex: hex(seen[0]!) })
    }

    await capture('pin', 'A pin the device does not have', null, () => board.services.device(1, 'IO', 'SET_LEVEL', { pin: 99, level: 'High' }))
    await capture('channel', 'A channel the DAC does not have', null, () => board.services.device(4, 'IO', 'SET_VOLTAGE', { pin: 5, voltage_mV: 100 }))
    await capture('device', 'A device that is not there', null, () => board.services.device(9, 'DEVICE', 'INFO'))
    await board.services.device(4, 'DEVICE', 'SUSPEND')
    await capture('suspended', 'A suspended device', null, () => board.services.device(4, 'IO', 'SET_VOLTAGE', { pin: 0, voltage_mV: 100 }))
    await board.services.device(4, 'DEVICE', 'RESUME')

    const compute = compiled.compute()
    await capture('load', 'A program the board refuses', 'compute', () => loadProgram(board.link, [compute.frames[0]!, ...compute.frames.slice(2)], { retries: 0 }).catch((e: unknown) => expect(e).toBeInstanceOf(LoadError)))
    const lamp = compiled.lamp()
    await capture('block', 'A block that fails while it runs', 'lamp', () => loadProgram(board.link, [...lamp.frames, engine.exec('normal')]))

    writeFileSync(OUT, `${JSON.stringify(samples, null, 2)}\n`)
  })
})
