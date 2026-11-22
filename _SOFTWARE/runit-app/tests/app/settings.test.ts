import { describe, expect, it } from 'vitest'
import { bundledEnums, bundledLinkDefinitions, bundledServiceCatalog, decodeBleTable, encodeFields, PROVIDER_COMMANDS, readProviderFrame, serviceCall } from '../../src/backend'
import type { GattService } from '../../src/app/project/projectFile'
import { emptySettings } from '../../src/app/project/projectFile'
import { bleProviders, gattApply, gattFromRecord, gattProblems, gattRecord, sameGatt } from '../../src/app/settings/gatt'
import { groupValues, sameValues, settingSections } from '../../src/app/settings/settings'
import { settingsRecords } from '../../src/app/settings/settingsRecords'

const catalog = bundledServiceCatalog()
const defs = bundledLinkDefinitions()
const problems: string[] = []
const sections = settingSections(catalog, problems)
const group = (key: string) => sections.flatMap((s) => s.groups).find((g) => g.key === key)
const table = (key: string) => sections.flatMap((s) => s.tables).find((t) => t.key === key)
const record = (name: string) => bundledEnums().settings_record!.members.find((m) => m.name === name)!.value

describe('the board settings from the service descriptors', () => {
  it('the time the app gives the board is a SYS command in the descriptors (tagged app), not a setting', () => {
    const set = serviceCall(catalog, 'SYS', 'TIME', { unix_ms: 1_760_000_000_123 }, 'write')
    expect([...set.request.body.subarray(2, 10)]).toEqual([...new Uint8Array(new BigUint64Array([1_760_000_000_123n]).buffer)])
    expect(catalog.service('SYS').commands.get('TIME')!.tags).toContain('app')
    expect(group('SYS.TIME')).toBeUndefined()
    expect(group('SYS.BYE')).toBeUndefined()
  })

  it('takes the mirrors whose read takes no key, each read answering what its write takes, one section per service', () => {
    expect(problems).toEqual([])
    for (const key of ['LOG.CONFIG', 'POWER_MANAGER.CONFIG', 'POWER_MANAGER.PSU', 'POWER_MANAGER.BATTERY', 'POWER_MANAGER.PD']) {
      const g = group(key)
      expect(g, key).toBeDefined()
      expect(g!.command.read && g!.command.write, key).toBeTruthy()
    }
    expect(sections.find((s) => s.service.id === 'LOG')?.title).toBe('Logs and errors')
  })

  it('a read with a key (I2C RAW: bus, address, register) is no setting', () => {
    expect(group('I2C.RAW')).toBeUndefined()
  })

  it('keeps a marked setting in its record; one without is the board\'s own', () => {
    expect(group('LOG.CONFIG')?.recordId).toBe(record('SYS_ACTIONS_ID_LOG'))
    expect(group('POWER_MANAGER.PSU')?.recordId).toBe(record('SYS_ACTIONS_ID_POWER_PSU'))
    expect(group('POWER_MANAGER.PD')?.recordId).toBe(record('SYS_ACTIONS_ID_POWER_PD'))
    // The dead-man of a session is set where the session is (the Connections window), not among the board's settings.
    expect(group('SYS.DEADMAN')).toBeUndefined()
    expect(sections.find((s) => s.service.id === 'BLE')?.record).toEqual({ id: record('SYS_ACTIONS_ID_BLE'), editor: 'ble' })
  })

  it('reads the response matrix as a table: a target per row, a column per power event kind, rows both ways', () => {
    const matrix = table('POWER_MANAGER.MATRIX')!
    expect(matrix.command.write!.args!.array).toBe(true)
    expect(matrix.target.id).toBe('target')
    expect(matrix.cells.indexEnum).toBe('sys_power_kind_e')
    expect(matrix.cells.choices?.length).toBe(bundledEnums().sys_power_response_e!.members.length)
    expect(matrix.recordId).toBe(record('SYS_ACTIONS_ID_POWER_RESPONSES'))
  })

  it('leaves out every write without the mark: nothing that only looks like a setting is ever sent as a read', () => {
    expect(group('ACTIONS.BEGIN')).toBeUndefined()
    expect(group('VM.SPEED')).toBeUndefined()
  })

  it('shows only the written fields of an answer, and compares values as the board gets them', () => {
    const log = group('LOG.CONFIG')!
    const written = groupValues(log, { level: 3, mirror_serial: true, trace_errors: false, other: 9 })
    expect(written).toEqual({ level: 3, mirror_serial: true, trace_errors: false })
    const board = groupValues(log, { level: 3, mirror_serial: true, trace_errors: false })
    expect(() => serviceCall(catalog, 'LOG', 'CONFIG', board, 'write')).not.toThrow()
    expect(sameValues(log.layout, board, { ...board, level: 3 })).toBe(true)
    expect(sameValues(log.layout, board, { ...board, mirror_serial: false })).toBe(false)
  })
})

describe('the settings the project keeps, as records of the stored project', () => {
  const gatt: GattService[] = [{ uuid: 0xee00, characteristics: [{ provider: 0x11, chr: 0xee01, framed: false, name: 'temp' }] }]
  it('builds the BLE record as PROVIDER_BLE.MD shows it: a PROVIDER CREATE of the BLE type with its cfg', () => {
    const frames = gattRecord(defs, catalog, gatt)
    expect(frames).toHaveLength(1)
    // [PROVIDER key][write][provider][op u16], then [type][flags] and provider_ble_cfg_t: svc, tx, rx, flags, rx_flags, name[16]
    expect(readProviderFrame(catalog, frames[0]!)).toMatchObject({ id: 0x11, type: { id: 'ble' }, config: { svc: 0xee00, chr: 0xee01, flags: 0, rx_flags: defs.ble.sendNoWait, name: 'temp' } })
    expect(frames[0]!.length).toBe(5 + 2 + catalog.provider('ble').config!.fixed)
    expect(gattFromRecord(defs, catalog, frames)).toEqual(gatt)
    expect(gattRecord(defs, catalog, [])).toEqual([])
  })

  it('reads the table of BLE LIST and the BLE providers of PROVIDER LIST', () => {
    const list = Uint8Array.of(2, 0xe0, 0xff, 0b11, 1, 0xe1, 0xff, 0b0111, 0x60, 0, 0x6c, 0x6f, 0x67, 0, 0x00, 0xee, 0b01, 1, 0x01, 0xee, 0b0100, 0x60, 0, 0x74, 0x65, 0x6d, 0x70, 0)
    const live = decodeBleTable(defs, list)
    expect(live[0]).toMatchObject({ uuid: 0xffe0, locked: true, characteristics: [{ uuid: 0xffe1, name: 'log', write: true, indicate: true, notify: true }] })
    expect(live[1]).toMatchObject({ uuid: 0xee00, locked: false, characteristics: [{ uuid: 0xee01, name: 'temp', notify: true, write: false }] })
    const ble = catalog.provider('ble').providerType!
    expect(bleProviders(catalog, Uint8Array.of(3, 1, ble, 3, 0x11, ble, 3, 5, ble + 1, 3))).toEqual([1, 0x11])
    expect(PROVIDER_COMMANDS.list).toBe('LIST')
  })

  it('applies by removing the providers of the project the board has, then creating them all; a table differs by its UUIDs', () => {
    expect(gattApply(defs, catalog, [0x11, 3], gatt).map((c) => c.command.id)).toEqual(['REMOVE', 'CREATE'])
    expect(gattApply(defs, catalog, [3], gatt).map((c) => c.command.id)).toEqual(['CREATE'])
    const live = decodeBleTable(defs, Uint8Array.of(1, 0x00, 0xee, 0b01, 1, 0x01, 0xee, 0b0100, 0x60, 0, 0x74, 0x65, 0x6d, 0x70, 0))
    expect(sameGatt(gatt, live)).toBe(true)
    expect(sameGatt([{ uuid: 0xee00, characteristics: [{ provider: 0x11, chr: 0xee02, framed: false, name: '' }] }], live)).toBe(false)
  })

  it('refuses a way whose provider is the board\'s own, another way\'s or a board link\'s', () => {
    const way = (provider: number, chr: number) => ({ provider, chr, framed: false, name: '' })
    expect(gattProblems(gatt, [0x30])).toEqual([])
    expect(gattProblems([{ uuid: 0xee00, characteristics: [way(1, 0xee01)] }])).toHaveLength(1)
    expect(gattProblems([{ uuid: 0xee00, characteristics: [way(0x11, 0xee01), way(0x11, 0xee02)] }])).toHaveLength(1)
    expect(gattProblems(gatt, [0x11])).toHaveLength(1)
  })

  it('writes each kept setting as its record', () => {
    const layout = catalog.service('POWER_MANAGER').commands.get('MATRIX')!.write!.args!
    const cells = layout.fields[1]!.size!
    const rows = [{ target: catalog.allDevices!, cells: Array.from({ length: cells }, () => 0) }, { target: 10, cells: Array.from({ length: cells }, (_, i) => (i === 3 ? 1 : 0)) }]
    const settings = {
      ...emptySettings(),
      values: { 'LOG.CONFIG': { level: 2, mirror_serial: true, trace_errors: false } },
      tables: { 'POWER_MANAGER.MATRIX': rows },
      gatt,
    }
    const records = settingsRecords(catalog, settings)
    expect(records.map((r) => r.id)).toEqual([record('SYS_ACTIONS_ID_LOG'), record('SYS_ACTIONS_ID_POWER_RESPONSES'), record('SYS_ACTIONS_ID_BLE')].sort((a, b) => records.findIndex((r) => r.id === a) - records.findIndex((r) => r.id === b)))
    // the matrix's write: [key][write][MATRIX], then each row's target (a device id, u8) and its cells
    const matrix = records.find((r) => r.id === record('SYS_ACTIONS_ID_POWER_RESPONSES'))!.frames[0]!
    expect(layout.fields[0]!.rawType).toBe('u8')
    expect([...matrix.subarray(3)]).toEqual(rows.flatMap((r) => [r.target, ...r.cells]))
  })

  it('writes nothing for what the project does not keep', () => {
    expect(settingsRecords(catalog, emptySettings())).toEqual([])
  })
})
