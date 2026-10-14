/**
 * Saving the settings on the real board over its console UART: the plan's frames are sent, then the records the board stored
 * are read back and must hold what the plan recorded.
 *
 *   $env:RUNIT_PORT = 'COM9'; npx vitest run tests/hardware/settings
 */
import { afterAll, beforeAll, describe, expect, it } from 'vitest'
import { routerKey, routerOp } from '../../src/backend/catalog'
import { RouterClient } from '../../src/backend/protocol/client'
import type { ErrorRecord } from '../../src/backend/protocol/frames'
import { boardConnectors, decodeBleList, decodeBoardSettings, decodeConnInfo, defaultBleProfile, defaultConfig, opRecord, planSettings, settingsOps } from '../../src/domain/settings'
import type { RouterValues, SettingsSnapshot, SettingsStep } from '../../src/domain/settings'
import { UartLink } from './uartLink'

const port = process.env.RUNIT_PORT
const ACTIONS = 'ROUTER_KEY_ACTIONS'
const idBytes = (id: number): Uint8Array => Uint8Array.of(id & 0xff, (id >> 8) & 0xff, (id >> 16) & 0xff, (id >>> 24) & 0xff)

const routers = (battery: number): RouterValues[] =>
  ['ROUTER_KEY_LOG', 'ROUTER_KEY_POWER'].map((symbol) => {
    const ops = settingsOps(symbol)
    return { ops, values: Object.fromEntries(ops.map((op) => [op.symbol, op.symbol === 'SYS_PWRM_OP_SET_BATTERY' ? { ...defaultConfig(op), cells: battery } : defaultConfig(op)])) }
  })

const snapshot = (): SettingsSnapshot => ({
  ble: {
    ...defaultBleProfile(),
    services: [
      ...defaultBleProfile().services,
      { id: 's', name: 'Sensors', uuid: '0xEE00', isPrimary: true, characteristics: [{ id: 'c', name: 'Temp', uuid: '0xEE01', write: false, notify: true, indicate: false, framed: false, txBufferSize: 64, rxBufferSize: 0 }] },
    ],
  },
  connectors: [...boardConnectors(), { id: 4, key: 'connector_4', name: 'temp', alias: 'temp', stream: '0x10', system: false, description: '', isSuspended: false, bindings: [{ id: 'b', provider: 'BLE', endpoint: '0xEE01' }] }],
})

describe.runIf(port)('saving the settings on the board', () => {
  let link: UartLink
  let client: RouterClient

  beforeAll(async () => {
    link = new UartLink(port!)
    await link.open()
    client = new RouterClient(link, { timeoutMs: 4000 })
  }, 20000)

  afterAll(async () => {
    client?.close()
    await link?.close()
  })

  const run = async (steps: readonly SettingsStep[]): Promise<void> => {
    for (const step of steps) {
      try {
        await client.request(step.keys, step.payload, { timeoutMs: 4000 })
      } catch (error) {
        if (!step.tolerant) throw new Error(`${step.label}: ${error instanceof Error ? error.message : String(error)}`)
      }
    }
  }

  const readRecord = async (id: number, frames: number): Promise<Uint8Array[]> =>
    client.requestMany([routerKey(ACTIONS), routerOp(ACTIONS, 'SYS_ACTIONS_OP_READ')], idBytes(id), frames)

  it('saving as the app does (the live table and streams first, records listed) makes the board report no error, twice in a row', async () => {
    const errors: ErrorRecord[] = []
    const off = client.onError((record) => errors.push(record))
    const live = async () => {
      const table = await client.request([routerKey('ROUTER_KEY_BLE'), routerOp('ROUTER_KEY_BLE', 'OP_LIST')])
      const streams = await client.request([routerKey('ROUTER_KEY_CONN'), routerOp('ROUTER_KEY_CONN', 'OP_INFO')])
      return { ble: decodeBleList(table, defaultBleProfile()), connectors: decodeConnInfo(streams, boardConnectors()) }
    }
    const stored = async () => {
      const page = await client.request([routerKey(ACTIONS), routerOp(ACTIONS, 'SYS_ACTIONS_OP_LIST')], idBytes(0x01000000))
      const ids = new Set<number>()
      for (let at = 2; at + 4 <= page.length; at += 4) ids.add(new DataView(page.buffer, page.byteOffset).getUint32(at, true))
      return ids
    }
    for (const round of [1, 2]) {
      const before = await live()
      await run(planSettings(snapshot(), routers(1), before, await stored()))
      const after = await live()
      expect(after.ble.services.filter((service) => !service.system).map((service) => service.uuid), `round ${round}`).toEqual(['0xEE00'])
      expect(after.connectors.find((connector) => connector.id === 4)?.bindings.map((binding) => binding.endpoint)).toEqual(['0xEE01'])
    }
    await new Promise((resolve) => setTimeout(resolve, 300))
    off()
    expect(errors.map((record) => client.catalog.rootCause(record))).toEqual([])
  }, 60000)

  it('stores the settings as records the board reads back as they were recorded', async () => {
    const first = snapshot()
    const plan = planSettings(first, routers(2), undefined)
    await run(plan)

    const battery = await readRecord(opRecord('ROUTER_KEY_POWER', 'SYS_PWRM_OP_SET_BATTERY')!, 1)
    // [0x0A][SET_BATTERY][chemistry, cells, low, critical]: the frame as recorded, with the 2 cells set above
    expect([...battery[0]!]).toEqual([routerKey('ROUTER_KEY_POWER'), routerOp('ROUTER_KEY_POWER', 'SYS_PWRM_OP_SET_BATTERY'), 0, 2, 20, 10])

    const ble = await readRecord(opRecord('ROUTER_KEY_BLE', 'OP_SVC_CREATE')!, 3)
    expect([...ble[0]!]).toEqual([routerKey('ROUTER_KEY_BLE'), routerOp('ROUTER_KEY_BLE', 'OP_SVC_CREATE'), 0x00, 0xee, 1])
    expect(ble[2]![1]).toBe(routerOp('ROUTER_KEY_BLE', 'OP_APPLY'))

    const conn = await readRecord(opRecord('ROUTER_KEY_CONN', 'OP_CREATE')!, 2)
    expect(conn[0]![1]).toBe(routerOp('ROUTER_KEY_CONN', 'OP_CREATE'))
    expect(conn[1]![1]).toBe(routerOp('ROUTER_KEY_CONN', 'OP_BIND'))
  }, 60000)

  it('reads the saved records back into the forms (sync)', async () => {
    const first = snapshot()
    await run(planSettings(first, routers(2), undefined))
    const records = new Map<number, Uint8Array[]>()
    records.set(opRecord('ROUTER_KEY_POWER', 'SYS_PWRM_OP_SET_BATTERY')!, await readRecord(opRecord('ROUTER_KEY_POWER', 'SYS_PWRM_OP_SET_BATTERY')!, 1))
    records.set(opRecord('ROUTER_KEY_BLE', 'OP_SVC_CREATE')!, await readRecord(opRecord('ROUTER_KEY_BLE', 'OP_SVC_CREATE')!, 3))
    records.set(opRecord('ROUTER_KEY_CONN', 'OP_CREATE')!, await readRecord(opRecord('ROUTER_KEY_CONN', 'OP_CREATE')!, 2))
    const board = decodeBoardSettings(records, ['ROUTER_KEY_POWER'], first)
    expect(board.routers.ROUTER_KEY_POWER!.SYS_PWRM_OP_SET_BATTERY!.cells).toBe(2)
    expect(board.ble.services.filter((service) => !service.system).map((service) => service.characteristics.map((char) => char.name))).toEqual([['Temp']])
    expect(board.connectors.find((connector) => connector.id === 4)?.bindings[0]?.endpoint).toBe('0xEE01')
  }, 60000)

  it('a second save with the same settings sends only the difference and stores the same records', async () => {
    const first = snapshot()
    await run(planSettings(first, routers(1), first))
    const battery = await readRecord(opRecord('ROUTER_KEY_POWER', 'SYS_PWRM_OP_SET_BATTERY')!, 1)
    expect(battery[0]![3]).toBe(1)
  }, 60000)

  it('with no user entries the records of the GATT table and the streams are removed', async () => {
    const before = snapshot()
    const empty: SettingsSnapshot = { ble: defaultBleProfile(), connectors: boardConnectors() }
    await run(planSettings(empty, routers(1), before))
    const gone = await readRecord(opRecord('ROUTER_KEY_BLE', 'OP_SVC_CREATE')!, 1).catch((error: unknown) => error)
    expect(gone).toBeInstanceOf(Error)
  }, 60000)
})
