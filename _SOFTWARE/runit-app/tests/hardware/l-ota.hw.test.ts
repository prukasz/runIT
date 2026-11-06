import { readFileSync } from 'node:fs'
import { fileURLToPath } from 'node:url'
import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { bundledServiceCatalog, confirmFirmware, loadProgram, otaInfo, serviceCall, sendProgram, writeFirmware } from '@backend'
import { blk, build, engine, num, project, read } from '../engine/helpers'
import { backUp, openBoard } from './harness'
import type { Board } from './harness'

/*
 * The firmware update on a board, over BLE: the image of the last build (runit-esp/build/runIT.bin) into the other slot and back.
 * Three times through the flash, so it takes minutes.
 */

const catalog = bundledServiceCatalog()
const image = readFileSync(fileURLToPath(new URL('../../../runit-esp/build/runIT.bin', import.meta.url)))

const restart = async (board: Board) => {
  await board.services.call('SYS', 'RESTART')
  await new Promise((resolve) => setTimeout(resolve, 2000))
  expect(await backUp(board, 40_000)).toBeDefined()
}

describe('sys_ota', () => {
  let board: Board
  beforeAll(async () => {
    board = await openBoard()
    await confirmFirmware(board.link, catalog)
  })
  afterAll(async () => {
    await board.close()
  })

  it('a program that runs refuses BEGIN', async () => {
    const result = build(project([num('x'), num('y')], [blk('s', 'set', { inputs: [read('x'), read('y')] })]))
    await loadProgram(board.link, result.frames)
    await sendProgram(board.link, [engine.exec('normal')])
    await expect(board.link.request(serviceCall(catalog, 'OTA', 'BEGIN', { size: 1000 }).request)).rejects.toThrow()
    await board.link.request(serviceCall(catalog, 'VM', 'RESET').request, { timeoutMs: 10_000 })
  })

  it('a write out of order, a COMMIT too early and an image that is no firmware are refused, and change nothing', async () => {
    const before = await otaInfo(board.link, catalog)
    const req = (op: string, args?: object) => board.link.request(serviceCall(catalog, 'OTA', op, args as never).request, { timeoutMs: 15_000 })
    await req('BEGIN', { size: 4096 })
    await expect(req('WRITE', { offset: 100, data: new Uint8Array(16) })).rejects.toThrow()
    await expect(req('COMMIT')).rejects.toThrow() // incomplete
    await req('BEGIN', { size: 16 })
    await expect(req('WRITE', { offset: 0, data: new Uint8Array(16) })).rejects.toThrow() // no image's first byte
    await req('ABORT')
    expect(await otaInfo(board.link, catalog)).toMatchObject({ slot: before.slot, open: false })
  })

  it('an update goes to the other slot, and is not confirmed until a client says so', async () => {
    const before = await otaInfo(board.link, catalog)
    expect(image.length).toBeLessThan(before.slotBytes)
    const started = Date.now()
    await writeFirmware(board.link, catalog, image)
    console.log(`OTA: ${image.length} B in ${((Date.now() - started) / 1000).toFixed(1)} s`)
    await restart(board)
    const after = await otaInfo(board.link, catalog)
    expect(after).toMatchObject({ slot: 1 - before.slot, state: 2 })
    expect(await confirmFirmware(board.link, catalog)).toBe(true)
    expect((await otaInfo(board.link, catalog)).state).toBe(1)
    await restart(board)
    expect(await otaInfo(board.link, catalog)).toMatchObject({ slot: after.slot, state: 1 })
  }, 300_000)

  it('a firmware not confirmed is gone after the next restart', async () => {
    const before = await otaInfo(board.link, catalog)
    await writeFirmware(board.link, catalog, image)
    await restart(board)
    expect(await otaInfo(board.link, catalog)).toMatchObject({ slot: 1 - before.slot, state: 2 })
    await restart(board)
    expect((await otaInfo(board.link, catalog)).slot).toBe(before.slot)
  }, 300_000)
})
