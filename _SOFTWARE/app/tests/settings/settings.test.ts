import { describe, expect, it } from 'vitest'
import {
  boardBleService,
  boardConnectors,
  boardSetup,
  configFromJson,
  configOp,
  configValueOk,
  curveFor,
  curveXAt,
  defaultConfig,
  encodeConfig,
  settingsOps,
  connectorModified,
  connectorsFromJson,
  connectorsModified,
  connectorsOnCharacteristic,
  defaultProjectSettings,
  profileFromJson,
  refreshConnectors,
  refreshProfile,
  removedBindings,
  restoreBinding,
  restoreConnector,
  restoreSystemConnectors,
  transportId,
} from '../../src/domain/settings'
import type { BleServiceSettings, ConnectorSettings } from '../../src/domain/settings'

const userService = (): BleServiceSettings => ({
  id: 'svc-1',
  name: 'Temperature',
  uuid: '0xEE00',
  isPrimary: true,
  characteristics: [{ id: 'chr-1', name: 'temp', uuid: '0xEE01', write: false, notify: true, indicate: false, framed: false, txBufferSize: 96, rxBufferSize: 0 }],
})

describe('the board setup', () => {
  it('has the four system connectors with the firmware IDs and stream bytes', () => {
    expect(boardConnectors().map((c) => [c.id, c.key, c.stream])).toEqual([[0, 'logs', '0x03'], [1, 'errors', '0x04'], [2, 'telemetry', '0x02'], [3, 'events', '0x06']])
  })

  it('binds them as runit_board_cfg.c does', () => {
    const byKey = Object.fromEntries(boardConnectors().map((c) => [c.key, c.bindings.map((b) => `${b.provider}@${b.endpoint}`).sort()]))
    expect(byKey).toEqual({
      logs: ['BLE@0xFFE3'],
      errors: ['BLE@0xFFE3', 'UART@Console (0)'],
      telemetry: ['BLE@0xFFE1', 'UART@Console (0)'],
      events: ['BLE@0xFFE1', 'UART@Console (0)'],
    })
  })

  it('has the locked runIT service with its four characteristics', () => {
    const service = boardBleService()
    expect(service).toMatchObject({ uuid: '0xFFE0', system: true, isPrimary: true })
    expect(service.characteristics.map((c) => [c.uuid, c.write, c.notify, c.framed])).toEqual([['0xFFE1', false, true, true], ['0xFFE2', true, false, true], ['0xFFE3', false, true, true], ['0xFFE4', false, true, false]])
    expect(service.characteristics.every((c) => c.system)).toBe(true)
  })

  it('knows the registries limits and the transport numbers', () => {
    expect(boardSetup().limits).toMatchObject({ connectorsMax: 8, bindingsPerConnector: 3, frameMax: 512 })
    expect(boardSetup().appBase).toBe(4)
    expect(transportId('UART')).toBe(1)
    expect(transportId('BLE')).toBe(2)
  })
})

describe('refreshing saved settings', () => {
  it('keeps the user services and puts the board service back as it is', () => {
    const tampered = boardBleService()
    tampered.characteristics[0]!.notify = false
    const refreshed = refreshProfile({ name: 'p', general: defaultProjectSettings().ble.general, services: [tampered, userService()] })
    expect(refreshed.services.map((s) => s.uuid)).toEqual(['0xFFE0', '0xEE00'])
    expect(refreshed.services[0]!.characteristics[0]!.notify).toBe(true)
  })

  it('drops a user service that claims the board UUID or a characteristic flagged as the board', () => {
    const impostor: BleServiceSettings = { ...userService(), uuid: '0xFFE0' }
    const refreshed = refreshProfile({ name: 'p', general: defaultProjectSettings().ble.general, services: [impostor] })
    expect(refreshed.services).toHaveLength(1)
    expect(refreshed.services[0]!.system).toBe(true)
  })

  it('keeps the bindings and pause of a system connector and the user connectors', () => {
    const saved: ConnectorSettings[] = [
      { ...boardConnectors()[2]!, isSuspended: true, bindings: [{ id: 'x', provider: 'BLE', endpoint: '0xFFE1' }] },
      { id: 4, key: 'mine', name: 'mine', alias: 'mine', stream: '0x10', system: false, description: '', isSuspended: false, bindings: [{ id: 'y', provider: 'BLE', endpoint: '0xEE01' }] },
    ]
    const refreshed = refreshConnectors(saved)
    expect(refreshed.map((c) => c.key)).toEqual(['logs', 'errors', 'telemetry', 'events', 'mine'])
    expect(refreshed[2]).toMatchObject({ isSuspended: true, bindings: [{ provider: 'BLE', endpoint: '0xFFE1' }] })
    expect(refreshed[4]!.bindings).toHaveLength(1)
  })
})

describe('what was removed from a system connector', () => {
  const withoutUart = (): ConnectorSettings[] => boardConnectors().map((c) => (c.key === 'telemetry' ? { ...c, bindings: c.bindings.filter((b) => b.provider === 'BLE') } : c))

  it('is found, compared by transport and endpoint', () => {
    const telemetry = withoutUart()[2]!
    expect(removedBindings(telemetry).map((b) => `${b.provider}@${b.endpoint}`)).toEqual(['UART@Console (0)'])
    expect(connectorModified(telemetry)).toBe(true)
    expect(connectorsModified(withoutUart())).toBe(true)
    expect(connectorsModified(boardConnectors())).toBe(false)
  })

  it('comes back one binding, one connector or all at once', () => {
    const telemetry = withoutUart()[2]!
    const one = restoreBinding(withoutUart(), 'telemetry', removedBindings(telemetry)[0]!.id)
    expect(one[2]!.bindings).toHaveLength(2)
    expect(restoreConnector(withoutUart(), 'telemetry')[2]!.bindings).toHaveLength(2)
    expect(connectorsModified(restoreSystemConnectors(withoutUart()))).toBe(false)
  })

  it('leaves the user connectors alone', () => {
    const mine: ConnectorSettings = { id: 4, key: 'mine', name: 'mine', alias: 'mine', stream: '0x10', system: false, description: '', isSuspended: false, bindings: [] }
    expect(restoreSystemConnectors([...withoutUart(), mine]).at(-1)).toBe(mine)
  })
})

describe('connectors on a characteristic', () => {
  it('lists the ones with a BLE binding at its UUID', () => {
    expect(connectorsOnCharacteristic(boardConnectors(), '0xFFE1').map((c) => c.key)).toEqual(['telemetry', 'events'])
    expect(connectorsOnCharacteristic(boardConnectors(), '0xFFE3').map((c) => c.key)).toEqual(['logs', 'errors'])
    expect(connectorsOnCharacteristic(boardConnectors(), '0xFFE2')).toEqual([])
  })
})

describe('reading saved copies', () => {
  it('reads a copy an older version saved (header, no framed flag)', () => {
    const legacyConnectors = JSON.stringify([{ id: 4, key: 'old', name: 'old', alias: 'Old', header: '0x20', system: false, description: '', direction: 'TX', maxPacketLen: 256, isSuspended: false, cMacro: 'X', bindings: [{ id: 'b', provider: 'BLE', endpoint: '0xEE01', direction: 'TX' }] }])
    expect(connectorsFromJson(legacyConnectors)!.at(-1)).toMatchObject({ key: 'old', stream: '0x20', bindings: [{ provider: 'BLE', endpoint: '0xEE01' }] })

    const legacyProfile = JSON.stringify({ name: 'x', general: {}, services: [{ id: 's', name: 'S', uuid: '0xEE00', isPrimary: true, advertised: false, characteristics: [{ id: 'c', name: 'C', uuid: '0xEE01', read: true, write: true, writeNoResponse: false, notify: false, indicate: false, txBufferSize: 0, rxBufferSize: 64, format: 'U8' }] }] })
    const profile = profileFromJson(legacyProfile)!
    expect(profile.services[1]!.characteristics[0]).toEqual({ id: 'c', name: 'C', uuid: '0xEE01', system: false, write: true, notify: false, indicate: false, framed: false, txBufferSize: 0, rxBufferSize: 64 })
    expect(profile.general.deviceName).toBe('runit')
  })

  it('drops what does not fit', () => {
    expect(connectorsFromJson('{"a":1}')).toBeUndefined()
    expect(profileFromJson('[]')).toBeUndefined()
  })
})

describe('settings built from the contracts', () => {
  const log = configOp('ROUTER_KEY_LOG', 'SYS_LOG_OP_SET')
  const power = configOp('ROUTER_KEY_POWER', 'SYS_PWRM_OP_SET_CONFIG')

  it('lists the fields of the C struct with their annotations', () => {
    expect(log.struct).toBe('sys_log_cfg_t')
    expect(log.fields.map((f) => f.name)).toEqual(['level', 'mirror_serial', 'trace_errors', 'send_errors', 'send_logs'])
    expect(log.fields[0]).toMatchObject({ alias: 'Level', choices: expect.arrayContaining([{ value: 3, alias: 'Info' }]) })
    expect(power.fields.find((f) => f.name === 'soft_margin_pct')).toMatchObject({ min: 0, max: 100, unit: '%' })
  })

  it('lists the ops the firmware tags @settings, in its order', () => {
    expect(settingsOps('ROUTER_KEY_LOG').map((op) => op.symbol)).toEqual(['SYS_LOG_OP_SET'])
    expect(settingsOps('ROUTER_KEY_POWER').map((op) => op.symbol)).toEqual(['SYS_PWRM_OP_SET_CONFIG', 'SYS_PWRM_OP_SET_PSU', 'SYS_PWRM_OP_SET_BATTERY'])
    expect(settingsOps('ROUTER_KEY_SYS')).toEqual([])
  })

  it('takes the choices of a field from the enum it names and finds the curve of a member', () => {
    const battery = configOp('ROUTER_KEY_POWER', 'SYS_PWRM_OP_SET_BATTERY')
    const chemistry = battery.fields.find((f) => f.name === 'chemistry')!
    expect(chemistry.choices).toEqual([{ value: 0, alias: 'None', symbol: 'SYS_POWER_BATTERY_NONE' }, { value: 1, alias: 'LiPo', symbol: 'SYS_POWER_BATTERY_LIPO' }])
    expect(curveFor('SYS_POWER_BATTERY_NONE')).toBeUndefined()
    const curve = curveFor('SYS_POWER_BATTERY_LIPO')!
    expect(curve.points).toHaveLength(21)
    expect(curveXAt(curve, 50)).toBe(3840)
    expect(curveXAt(curve, 100)).toBe(4200)
    expect(curveXAt(curve, 101)).toBeUndefined()
  })

  it('leaves out what is not one number, a pin of a device, and marks what the board decides (@hidden) so the form skips it', () => {
    expect(power.fields.some((f) => f.name === 'source_pin')).toBe(false)
    expect(power.skipped).toEqual([])
    expect(power.fields.filter((f) => f.hidden).map((f) => f.name)).toEqual(['battery_level'])
  })

  it('starts from the defaults annotated in C (@default)', () => {
    expect(defaultConfig(log)).toEqual({ level: 3, mirror_serial: 1, trace_errors: 1, send_errors: 1, send_logs: 1 })
    expect(defaultConfig(power)).toEqual({ enabled: 1, strict: 1, soft_margin_pct: 20, battery_level: 1, pd_max_auto: 1, ext_input: 0 })
  })

  it('accepts a listed choice or a value inside the limits and nothing else', () => {
    const level = log.fields[0]!
    const margin = power.fields.find((f) => f.name === 'soft_margin_pct')!
    expect(configValueOk(level, 5)).toBe(true)
    expect(configValueOk(level, 6)).toBe(false)
    expect(configValueOk(margin, 100)).toBe(true)
    expect(configValueOk(margin, 101)).toBe(false)
    expect(configValueOk(margin, 1.5)).toBe(false)
  })

  it('reads a saved copy over the defaults', () => {
    expect(configFromJson(log, '{"level":1,"send_logs":0,"nope":7}')).toEqual({ level: 1, mirror_serial: 1, trace_errors: 1, send_errors: 1, send_logs: 0 })
    expect(configFromJson(log, '{"level":99}')).toMatchObject({ level: 3 })
    expect(configFromJson(log, '[]')).toBeUndefined()
  })

  it('packs the struct as the firmware reads it, pin bytes left at 0', () => {
    expect(Array.from(encodeConfig(log, defaultConfig(log)))).toEqual([3, 1, 1, 1, 1])
    expect(Array.from(encodeConfig(power, { ...defaultConfig(power), soft_margin_pct: 35 }))).toEqual([1, 1, 35, 0, 0, 0, 1, 1, 0])
  })
})
