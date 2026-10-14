import { describe, expect, it } from 'vitest'
import { boardConnectors, decodeBleList, decodeBoardSettings, decodeConnInfo, defaultBleProfile, defaultConfig, planSettings, settingsOps, transportId } from '../../src/domain/settings'
import type { BleProfile, ConnectorSettings, RouterValues, SettingsSnapshot } from '../../src/domain/settings'
import { routerKey, routerOp } from '../../src/backend/catalog'

const routers = (): RouterValues[] =>
  ['ROUTER_KEY_LOG', 'ROUTER_KEY_POWER'].map((symbol) => {
    const ops = settingsOps(symbol)
    return { ops, values: Object.fromEntries(ops.map((op) => [op.symbol, defaultConfig(op)])) }
  })

const withService = (profile: BleProfile): BleProfile => ({
  ...profile,
  services: [
    ...profile.services,
    {
      id: 's', name: 'Sensors', uuid: '0xEE00', isPrimary: true,
      characteristics: [{ id: 'c', name: 'Temp', uuid: '0xEE01', write: false, notify: true, indicate: false, framed: false, txBufferSize: 64, rxBufferSize: 0 }],
    },
  ],
})

const userStream = (): ConnectorSettings => ({ id: 4, key: 'connector_4', name: 'temp', alias: 'temp', stream: '0x10', system: false, description: '', isSuspended: false, bindings: [{ id: 'b', provider: 'BLE', endpoint: '0xEE01' }] })

const snapshot = (ble: BleProfile, connectors: readonly ConnectorSettings[] = boardConnectors()): SettingsSnapshot => ({ ble, connectors })
const labels = (steps: { label: string }[]) => steps.map((step) => step.label)
/** The changes applied now: what comes before the records of the `ble` and `conn` routers. */
const live = <T extends { label: string }>(steps: T[]): T[] => steps.slice(0, steps.findIndex((step) => /^(Record|Remove) Bluetooth services$/.test(step.label)))

describe('planSettings', () => {
  it('records every settings op as the record the firmware names, executed as it is recorded', () => {
    const steps = planSettings(snapshot(defaultBleProfile()), routers(), undefined)
    expect(labels(steps).slice(0, 3)).toEqual(['Record Set log options', 'Set log options', 'Store Set log options'])
    const start = steps[0]!
    expect(start.keys).toEqual([routerKey('ROUTER_KEY_ACTIONS'), routerOp('ROUTER_KEY_ACTIONS', 'SYS_ACTIONS_OP_RECORD_START')])
    // record id 0x01000001 (SYS_ACTIONS_ID_LOG), execute 1
    expect([...start.payload!]).toEqual([0x01, 0x00, 0x00, 0x01, 1])
    const battery = steps.find((step) => step.label === 'Set battery')!
    // the battery frame: chemistry 0, 1 cell, low 20 %, critical 10 % (the @default values in C)
    expect([...battery.payload!]).toEqual([0, 1, 20, 10])
    expect(battery.recorded).toBe(true)
    const batteryStart = steps[steps.findIndex((step) => step === battery) - 1]!
    expect([...batteryStart.payload!]).toEqual([0x04, 0x00, 0x00, 0x01, 1])
  })

  it('removes the records of the board own services and streams when the user has none', () => {
    const steps = planSettings(snapshot(defaultBleProfile()), [], undefined)
    expect(labels(steps)).toEqual(['Remove Bluetooth services', 'Remove data streams'])
    expect(steps.every((step) => step.tolerant)).toBe(true)
  })

  it('creates a user service, its characteristic, applies, then creates and binds the stream, and records the same without running it', () => {
    const steps = planSettings(snapshot(withService(defaultBleProfile()), [...boardConnectors(), userStream()]), [], undefined)
    expect(labels(live(steps))).toEqual([
      'Remove stream temp', 'Remove service 0xEE00', 'Create service 0xEE00', 'Create characteristic 0xEE01', 'Apply the GATT table',
      'Create stream temp', 'Bind temp to BLE 0xEE01',
    ])
    expect(live(steps).filter((step) => step.tolerant).length).toBe(2)
    expect(live(steps).find((step) => step.reconnects)?.label).toBe('Apply the GATT table')
    // the Apply inside the record is only stored: it must not make the app wait for a drop
    expect(steps.filter((step) => step.reconnects).length).toBe(1)
    const char = steps.find((step) => step.label.startsWith('Create characteristic'))!
    // svc 0xEE00, uuid 0xEE01, write 0, indicate 0, notify 1, framed 0, tx 64, rx 0, then the name and its NUL
    expect([...char.payload!]).toEqual([0x00, 0xee, 0x01, 0xee, 0, 0, 1, 0, 64, 0, 0, 0, 0, 0, 0, 0, ...new TextEncoder().encode('Temp'), 0])
    const bind = steps.find((step) => step.label.startsWith('Bind'))!
    expect([...bind.payload!]).toEqual([4, transportId('BLE'), 0x01, 0xee, 0, 0])
    const rest = labels(steps.slice(live(steps).length))
    expect(rest).toEqual([
      'Record Bluetooth services', 'Create service 0xEE00', 'Create characteristic 0xEE01', 'Apply the GATT table', 'Store Bluetooth services',
      'Record data streams', 'Create stream temp', 'Bind temp to BLE 0xEE01', 'Store data streams',
    ])
    // recorded, not run: execute 0, record id 0x01000006 (SYS_ACTIONS_ID_BLE)
    expect([...steps.find((step) => step.label === 'Record Bluetooth services')!.payload!]).toEqual([0x06, 0x00, 0x00, 0x01, 0])
  })

  it('applies only the difference from the baseline and still stores the whole set', () => {
    const base = snapshot(withService(defaultBleProfile()), [...boardConnectors(), userStream()])
    expect(live(planSettings(base, [], base))).toEqual([])
    const editedProfile = withService(defaultBleProfile())
    editedProfile.services[1]!.characteristics[0] = { ...editedProfile.services[1]!.characteristics[0]!, txBufferSize: 128 }
    expect(labels(live(planSettings(snapshot(editedProfile, base.connectors), [], base)))).toEqual(['Remove characteristic 0xEE01', 'Create characteristic 0xEE01', 'Apply the GATT table'])
    expect(labels(live(planSettings(snapshot(defaultBleProfile(), boardConnectors()), [], base)))).toEqual(['Remove stream temp', 'Remove service 0xEE00', 'Apply the GATT table'])
  })

  it('re-addresses and pauses a stream of the board without removing it', () => {
    const base = snapshot(defaultBleProfile())
    const connectors = boardConnectors().map((c) => (c.key === 'telemetry' ? { ...c, isSuspended: true } : c))
    const steps = planSettings(snapshot(defaultBleProfile(), connectors), [], base)
    expect(labels(live(steps))).toEqual(['Suspend Telemetry'])
    expect(labels(steps).slice(-3)).toEqual(['Record data streams', 'Suspend Telemetry', 'Store data streams'])
  })
})

describe('reading the records back (sync)', () => {
  /** What a board would hold after the plan: the frames between each RECORD_START and RECORD_STOP, by record id. */
  const stored = (steps: { label: string; payload?: Uint8Array; keys: readonly number[]; recorded?: boolean }[]) => {
    const records = new Map<number, Uint8Array[]>()
    let open: Uint8Array[] | undefined
    for (const step of steps) {
      if (step.label.startsWith('Record ')) {
        const id = new DataView(step.payload!.buffer, step.payload!.byteOffset).getUint32(0, true)
        open = []
        records.set(id, open)
      } else if (step.label.startsWith('Store ')) open = undefined
      else if (open && step.recorded) open.push(Uint8Array.of(...step.keys, ...(step.payload ?? [])))
    }
    return records
  }

  it('gives back the settings, services, characteristics and streams that were saved', () => {
    const current = snapshot(withService(defaultBleProfile()), [...boardConnectors().map((c) => (c.key === 'telemetry' ? { ...c, isSuspended: true } : c)), userStream()])
    const values = routers()
    const battery = values[1]!.ops.find((op) => op.symbol === 'SYS_PWRM_OP_SET_BATTERY')!
    const edited = { ...values[1]!, values: { ...values[1]!.values, [battery.symbol]: { ...values[1]!.values[battery.symbol]!, cells: 3 } } }
    const board = decodeBoardSettings(stored(planSettings(current, [values[0]!, edited], undefined)), ['ROUTER_KEY_LOG', 'ROUTER_KEY_POWER'], current)
    expect(board.routers.ROUTER_KEY_POWER!.SYS_PWRM_OP_SET_BATTERY).toEqual({ chemistry: 0, cells: 3, low_pct: 20, critical_pct: 10 })
    const user = board.ble.services.filter((service) => !service.system)
    expect(user.map((service) => [service.uuid, service.isPrimary, service.characteristics.map((char) => [char.uuid, char.name, char.notify, char.txBufferSize])])).toEqual([['0xEE00', true, [['0xEE01', 'Temp', true, 64]]]])
    expect(board.connectors.find((c) => c.key === 'telemetry')!.isSuspended).toBe(true)
    const temp = board.connectors.find((c) => c.id === 4)!
    expect([temp.name, temp.stream, temp.bindings.map((b) => [b.provider, b.endpoint])]).toEqual(['temp', '0x10', [['BLE', '0xEE01']]])
  })

  it('a board with no records is the default board', () => {
    const board = decodeBoardSettings(new Map(), ['ROUTER_KEY_LOG'], snapshot(defaultBleProfile()))
    expect(board.routers.ROUTER_KEY_LOG).toEqual({})
    expect(board.ble.services.length).toBe(1)
    expect(board.connectors).toEqual(boardConnectors())
  })
})

describe('what the board runs now (ble LIST, conn INFO)', () => {
  it('reads the live GATT table and leaves the board service to the board setup', () => {
    // two services: the locked runIT one (flags 3) with one characteristic, a user one (primary) with Temp
    const runit = [0xe0, 0xff, 3, 1, 0xe1, 0xff, 0x0c, 0, 2, 0, 0, 0, 0, 0, 0, ...new TextEncoder().encode('runit TX'), 0]
    const user = [0x00, 0xee, 1, 1, 0x01, 0xee, 0x04, 64, 0, 0, 0, 0, 0, 0, 0, ...new TextEncoder().encode('Temp'), 0]
    const profile = decodeBleList(Uint8Array.of(2, ...runit, ...user), defaultBleProfile())
    expect(profile.services[0]).toEqual(defaultBleProfile().services[0])
    expect(profile.services.slice(1).map((s) => [s.uuid, s.isPrimary, s.characteristics.map((c) => [c.uuid, c.name, c.notify, c.write, c.txBufferSize])])).toEqual([['0xEE00', true, [['0xEE01', 'Temp', true, false, 64]]]])
  })

  it('reads the live streams: a re-addressed board stream, a paused one and a user one', () => {
    const telemetry = boardConnectors().find((c) => c.key === 'telemetry')!
    const entry = (id: number, stream: number, flags: number, bindings: number[][], name: string) => [id, stream, flags, bindings.length, ...bindings.flat(), ...new TextEncoder().encode(name), 0]
    const ble = transportId('BLE')
    const data = Uint8Array.of(2, ...entry(telemetry.id, Number(telemetry.stream), 1 | 2, [[ble, 0x01, 0xee, 0, 0]], 'telemetry'), ...entry(4, 0x10, 0, [[ble, 0x01, 0xee, 0, 0]], 'temp'))
    const connectors = decodeConnInfo(data, [])
    const live = connectors.find((c) => c.key === 'telemetry')!
    expect([live.isSuspended, live.bindings.map((b) => [b.provider, b.endpoint])]).toEqual([true, [['BLE', '0xEE01']]])
    expect(connectors.find((c) => c.id === 4)).toMatchObject({ name: 'temp', stream: '0x10', system: false })
  })

  it('with the live state as the baseline nothing is removed that the board lacks, and a record is removed only when listed', () => {
    const current = snapshot(defaultBleProfile())
    const steps = planSettings(current, [], { ble: defaultBleProfile(), connectors: boardConnectors() }, new Set())
    expect(steps).toEqual([])
  })
})
