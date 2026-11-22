import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { bundledServiceCatalog, CommandError, decodeBleTable, decodePeers, sidesOf } from '@backend'
import { openBoard } from './harness'
import type { Board } from './harness'

/*
 * The services and the device the devkit has (runit-esp/components/app/runit, RUNIT.MD): device 0 is the ESP32-S3's
 * GPIO, the providers are the console and the runIT BLE pair, the sessions and the data matrix are the board's setup.
 */

const GPIO = 0
const PIN = 4 // a free pin of the devkit

const catalog = bundledServiceCatalog()
let board: Board
beforeAll(async () => {
  board = await openBoard()
})
afterAll(async () => {
  await board.close()
})

const refused = async (promise: Promise<unknown>): Promise<CommandError> => {
  const error = await promise.then(
    () => undefined,
    (e: unknown) => e,
  )
  expect(error).toBeInstanceOf(CommandError)
  return error as CommandError
}

const level = async (pin = PIN) => ((await board.services.device(GPIO, 'IO', 'LEVEL', { pin }, { side: 'read' })).value as { level: unknown }).level

describe('the catalog has no problem and the services it names are on the board', () => {
  it('no problems in the bundle', () => {
    expect(catalog.problems).toEqual([])
  })

  it('every read of a service that takes no arguments answers and decodes by its descriptor', async () => {
    const answered: string[] = []
    for (const service of catalog.services.values()) {
      if (service.contractId !== undefined) continue // addressed by an id: below
      for (const command of service.commands.values()) {
        if (!command.read || command.read.args) continue
        const call = board.services.call(service.id, command.id, undefined, { side: 'read' })
        // a service the board has no use for yet (the power manager of a devkit) says so with an error record: it is answered all the same
        const result = await call.then(
          () => 'ok',
          (e: unknown) => (e instanceof CommandError && e.code === 'device-error' && e.record ? 'refused' : String(e)),
        )
        expect(['ok', 'refused'], `${service.id} ${command.id}`).toContain(result)
        answered.push(`${service.id} ${command.id}`)
      }
    }
    expect(answered.length).toBeGreaterThan(8)
  })
})

describe('the board and its ways', () => {
  it('this client is an open session on the BLE provider; the console is a session with nobody on it', async () => {
    const { data } = await board.services.call('SESSION', 'PEERS')
    const peers = decodePeers(data, board.services.catalog)
    expect(peers.find((p) => p.open && p.peerId === board.link.session!.clientId)).toBeDefined()
    expect(peers.length).toBeGreaterThanOrEqual(2)
  })

  it('SESSION ID is the board id the hello said', async () => {
    const { value } = await board.services.call('SESSION', 'ID', undefined, { side: 'read' })
    expect((value as { board_id: number }).board_id).toBe(board.link.session!.boardId)
  })

  it('PROVIDER LIST has the console and the BLE providers', async () => {
    const { value, data } = await board.services.call('PROVIDER', 'LIST')
    const { lead, rows } = value as { lead: number; rows: Array<{ provider: number; type: number; state: number }> }
    expect(lead).toBe(rows.length)
    expect(rows.length).toBeGreaterThanOrEqual(4)
    expect(data.length).toBe(1 + rows.length * 3)
    const bleType = catalog.providers.get('ble')!.providerType!
    expect(rows.filter((r) => r.type === bleType).length).toBeGreaterThanOrEqual(3)
    expect(rows.some((r) => r.type !== bleType)).toBe(true)
  })

  it('DATA_MATRIX LIST has the roles and the routes of the setup', async () => {
    const { data } = await board.services.call('DATA_MATRIX', 'LIST')
    expect(data.length).toBeGreaterThan(8)
  })

  it('BLE LIST is the one runIT service with its four characteristics', async () => {
    const { data } = await board.services.call('BLE', 'LIST')
    const table = decodeBleTable(board.link.defs, data)
    expect(table.length).toBe(1)
    expect(table[0]!.uuid).toBe(board.link.defs.gatt.service)
    const uuids = table[0]!.characteristics.map((c) => c.uuid)
    expect(uuids.length).toBe(4)
    expect(uuids).toContain(board.link.defs.gatt.tx)
    expect(uuids).toContain(board.link.defs.gatt.rx)
    expect(uuids).toContain(board.link.defs.gatt.logs)
    expect(uuids).toContain(board.link.defs.gatt.telemetry)
  })
})

describe('the GPIO device', () => {
  it('device 0 is the ESP32-S3 GPIO, and its OPS list is not empty', async () => {
    const info = (await board.services.device(GPIO, 'DEVICE', 'INFO')).value as { type_id: number; suspended: boolean }
    expect(info.type_id).toBe(catalog.device('esp32s3_gpio').typeId)
    expect(info.suspended).toBe(false)
    const { data } = await board.services.device(GPIO, 'DEVICE', 'OPS')
    expect(data.length).toBeGreaterThan(0)
    expect(data.length % 3).toBe(0)
  })

  it('a pin set as an output is written, toggled and read back each time', async () => {
    await board.services.device(GPIO, 'IO', 'RESET', { pin: PIN }) // a pin a run before set up is set up again
    await board.services.device(GPIO, 'IO', 'MODE', { pin: PIN, mode: 'SYS_IO_MODE_OUTPUT_PUSH_PULL' }, { side: 'write' })
    await board.services.device(GPIO, 'IO', 'LEVEL', { pin: PIN, level: 'Low' }, { side: 'write' })
    expect(String(await level())).toMatch(/Low|0|false/i)
    await board.services.device(GPIO, 'IO', 'TOGGLE', { pin: PIN })
    expect(String(await level())).toMatch(/High|1|true/i)
    await board.services.device(GPIO, 'IO', 'TOGGLE', { pin: PIN })
    expect(String(await level())).toMatch(/Low|0|false/i)
  })

  it('a pin the chip does not have is refused with the error named', async () => {
    const error = await refused(board.services.device(GPIO, 'IO', 'LEVEL', { pin: 99, level: 'High' }, { side: 'write' }))
    expect(error.record!.cause!.info?.name).toBe('ERR_DEV_BAD_PIN')
  })

  it('a device that is not there is refused', async () => {
    const error = await refused(board.services.device(30, 'DEVICE', 'INFO'))
    expect(error.record!.chain.map((n) => n.info?.name)).toContain('ERR_DEV_NOT_FOUND')
  })

  it('SUSPEND refuses a command; RESUME takes it again', async () => {
    await board.services.device(GPIO, 'DEVICE', 'SUSPEND')
    try {
      const error = await refused(board.services.device(GPIO, 'IO', 'TOGGLE', { pin: PIN }))
      expect(error.record!.cause!.info?.name).toBe('ERR_DEV_SUSPENDED')
    } finally {
      await board.services.device(GPIO, 'DEVICE', 'RESUME')
    }
    expect((await board.services.device(GPIO, 'DEVICE', 'INFO')).value).toMatchObject({ suspended: false })
  })

  it('a second device with an id in use is refused, and nothing changes', async () => {
    const error = await refused(board.services.create(GPIO, 'esp32s3_gpio'))
    expect(error.record).toBeDefined()
    expect((await board.services.device(GPIO, 'DEVICE', 'INFO')).value).toMatchObject({ type_id: catalog.device('esp32s3_gpio').typeId })
  })

  it('a pin out of the byte range is refused by the encoder: nothing is sent', () => {
    expect(() => board.services.device(GPIO, 'IO', 'LEVEL', { pin: 300, level: 'High' }, { side: 'write' })).toThrow()
  })
})

describe('commands of every kind have their sides in the descriptor', () => {
  it('every service command has a side', () => {
    for (const service of catalog.services.values()) for (const command of service.commands.values()) expect(sidesOf(command).length, `${service.id} ${command.id}`).toBeGreaterThan(0)
  })
})
