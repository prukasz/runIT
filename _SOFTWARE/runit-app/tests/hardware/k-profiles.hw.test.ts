import { afterAll, describe, expect, it } from 'vitest'
import { bundledServiceCatalog } from '@backend'
import { bundledBoard } from '../../src/app/devices/boardDescriptor'
import type { BoardKind } from '../../src/app/devices/boardDescriptor'
import { bootSteps, profileFromBoard } from '../../src/app/devices/boardEditor'
import { boardHasProfile, writeBoardProfile } from '../../src/app/devices/boardProfile'
import { backUp, openBoard, sleep } from './harness'
import type { Board } from './harness'

/*
 * The bundled boards' profiles on the real board: each is written as the app writes it (the JSON, then the record), the board
 * restarts (only a boot creates the profile's devices), and then every device of the profile must exist (one LIST, no INFO to
 * an ID the board has not) and the errors the boot left are read. The devkit's is the GPIO alone; the PCB's is every chip, so
 * it needs the runIT PCB: on a bare devkit the chips' errors are the board's, not the app's.
 */

const catalog = bundledServiceCatalog()
let board: Board | undefined
afterAll(async () => {
  await board?.close()
})

/** The errors the board kept: its count, then each record's first bytes. */
const keptErrors = async (b: Board): Promise<number> => {
  const { data } = await b.services.call('LOG', 'ERRORS', { index: 0 }, { timeoutMs: 3000, label: 'LOG ERRORS' })
  return data[0] ?? 0
}

describe.each(['devkit', 'pcb'] as BoardKind[])('the %s profile', (kind) => {
  it('is written, the board restarts, and its devices exist', async () => {
    const description = bundledBoard(kind)
    const profile = profileFromBoard(description, catalog)
    const boot = bootSteps(profile)
    board ??= await openBoard()
    const b = board

    await writeBoardProfile(b.link, catalog, description, profile, boot)
    expect(await boardHasProfile(b.link, catalog, description, profile, boot)).toBe(true)
    await b.services.call('SYS', 'RESTART') // answered, then the board restarts about 100 ms later
    await sleep(1500)
    expect(await backUp(b, 30_000)).toBeDefined()
    await sleep(2000) // the boot creates its devices before the link is useful; give its steps time

    const { value } = await b.services.device(catalog.allDevices!, 'DEVICE', 'LIST', undefined, { side: 'read' })
    const rows = value as readonly { id: number; type_id: number }[]
    const have = new Set(rows.map((r) => r.id))
    const missing = description.devices.map((d) => d.id).filter((id) => !have.has(id))
    const errors = await keptErrors(b)
    console.log(`[${kind}] ${description.devices.length} devices in the profile, ${rows.length} on the board, missing ${JSON.stringify(missing)}, errors kept ${errors}`)
    expect(missing).toEqual([])
    expect(await boardHasProfile(b.link, catalog, description, profile, boot)).toBe(true)
    // The count means something: an INFO to an ID the board has not is one more kept error.
    await b.services.device(60, 'DEVICE', 'INFO', undefined, { side: 'read' }).catch(() => undefined)
    await sleep(300)
    expect(await keptErrors(b)).toBeGreaterThan(errors)
  })
})
