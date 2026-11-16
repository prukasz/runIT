import { describe, expect, it } from 'vitest'
import { bundledEnums, bundledLinkDefinitions, bundledServiceCatalog, encodeFields, serviceCall } from '../../src/backend'
import type { GattService } from '../../src/app/project/projectFile'
import { emptySettings } from '../../src/app/project/projectFile'
import { decodeBleProviders, decodeGattList, gattApply, gattFromRecord, gattRecord, sameGatt } from '../../src/app/settings/gatt'
import { groupValues, sameValues, settingSections } from '../../src/app/settings/settings'
import { settingsRecords } from '../../src/app/settings/settingsRecords'

const catalog = bundledServiceCatalog()
const problems: string[] = []
const sections = settingSections(catalog, problems)
const group = (key: string) => sections.flatMap((s) => s.groups).find((g) => g.key === key)
const table = (key: string) => sections.flatMap((s) => s.tables).find((t) => t.key === key)
const record = (name: string) => bundledEnums().settings_record!.members.find((m) => m.name === name)!.value

describe('the board settings from the service descriptors', () => {
  it('the time the app gives the board is a SYS command in the descriptors (tagged app), not a setting', () => {
    const set = serviceCall(catalog, 'SYS', 'TIME', { unix_ms: 1_760_000_000_123 }, 'write')
    expect([...set.request.payload!.subarray(0, 8)]).toEqual([...new Uint8Array(new BigUint64Array([1_760_000_000_123n]).buffer)])
    expect(catalog.service('SYS').commands.get('TIME')!.tags).toContain('app')
    expect(group('SYS.TIME')).toBeUndefined()
    expect(group('SYS.BYE')).toBeUndefined()
  })

  it('takes the mirrors whose read takes no key, each read answering what its write takes, one section per service', () => {
    expect(problems).toEqual([])
    for (const key of ['LOG.CONFIG', 'POWER_MANAGER.CONFIG', 'POWER_MANAGER.PSU', 'POWER_MANAGER.BATTERY', 'POWER_MANAGER.PD', 'SYS.DEADMAN']) {
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
    // The dead-man is runtime only: the project never keeps it, so a board that restarts runs offline.
    expect(group('SYS.DEADMAN')?.recordId).toBeUndefined()
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
    const written = groupValues(log, { level: 3, mirror_serial: true, trace_errors: false, send_errors: true, send_logs: false, other: 9 })
    expect(written).toEqual({ level: 3, mirror_serial: true, trace_errors: false, send_errors: true, send_logs: false })
    const board = groupValues(log, { level: 3, mirror_serial: true, trace_errors: false, send_errors: true, send_logs: false })
    expect(() => serviceCall(catalog, 'LOG', 'CONFIG', board, 'write')).not.toThrow()
    expect(sameValues(log.layout, board, { ...board, level: 3 })).toBe(true)
    expect(sameValues(log.layout, board, { ...board, send_logs: true })).toBe(false)
  })
})

describe('the settings the project keeps, as records of the stored project', () => {
  const gatt: GattService[] = [{ uuid: 0xee00, characteristics: [{ provider: 0x11, tx: 0xee01, rx: 0, framed: false, name: 'temp' }] }]
  it('builds the BLE record as PROVIDER_BLE.MD shows it: a PROVIDER CREATE of type BLE with its cfg', () => {
    const frames = gattRecord(catalog, gatt)
    expect(frames).toHaveLength(1)
    // [PROVIDER key][write][provider][op u16: contract 13, CREATE 0][type BLE][flags], then provider_ble_cfg_t: svc, tx, rx, flags, name[16]
    expect([...frames[0]!]).toEqual([0x0d, 0x01, 0x11, 0x00, 0x0d, 0x02, 0x00, 0x00, 0xee, 0x01, 0xee, 0, 0, 0, 0x74, 0x65, 0x6d, 0x70, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0])
    expect(gattFromRecord(catalog, frames)).toEqual(gatt)
    expect(gattRecord(catalog, [])).toEqual([])
  })

  it('reads the table of BLE LIST and the BLE providers of PROVIDER LIST', () => {
    const list = Uint8Array.of(2, 0xe0, 0xff, 0b11, 1, 0xe1, 0xff, 0b0111, 0x60, 0, 0x6c, 0x6f, 0x67, 0, 0x00, 0xee, 0b01, 1, 0x01, 0xee, 0b0100, 0x60, 0, 0x74, 0x65, 0x6d, 0x70, 0)
    const live = decodeGattList(list)
    expect(live[0]).toMatchObject({ uuid: 0xffe0, locked: true, characteristics: [{ uuid: 0xffe1, name: 'log', write: true, indicate: true, notify: true }] })
    expect(live[1]).toMatchObject({ uuid: 0xee00, locked: false, characteristics: [{ uuid: 0xee01, name: 'temp', notify: true, write: false }] })
    expect(decodeBleProviders(Uint8Array.of(3, 1, 2, 3, 0x11, 2, 3, 5, 1, 3))).toEqual([1, 0x11]) // type 2: BLE
  })

  it('applies by removing the providers of the project the board has, then creating them all; a table differs by its UUIDs', () => {
    expect(gattApply(catalog, [0x11, 3], gatt).map((c) => c.command.id)).toEqual(['REMOVE', 'CREATE'])
    expect(gattApply(catalog, [3], gatt).map((c) => c.command.id)).toEqual(['CREATE'])
    const live = decodeGattList(Uint8Array.of(1, 0x00, 0xee, 0b01, 1, 0x01, 0xee, 0b0100, 0x60, 0, 0x74, 0x65, 0x6d, 0x70, 0))
    expect(sameGatt(gatt, live)).toBe(true)
    expect(sameGatt([{ uuid: 0xee00, characteristics: [{ provider: 0x11, tx: 0xee02, rx: 0, framed: false, name: '' }] }], live)).toBe(false)
  })

  it('writes each kept setting as its record', () => {
    const rows = [{ target: 0xffff, cells: Array.from({ length: 11 }, () => 0) }, { target: 10, cells: Array.from({ length: 11 }, (_, i) => (i === 3 ? 1 : 0)) }]
    const settings = {
      ...emptySettings(),
      values: { 'LOG.CONFIG': { level: 2, mirror_serial: true, trace_errors: false, send_errors: true, send_logs: false } },
      tables: { 'POWER_MANAGER.MATRIX': rows },
      gatt,
    }
    const records = settingsRecords(catalog, settings)
    expect(records.map((r) => r.id)).toEqual([record('SYS_ACTIONS_ID_LOG'), record('SYS_ACTIONS_ID_POWER_RESPONSES'), record('SYS_ACTIONS_ID_BLE')].sort((a, b) => records.findIndex((r) => r.id === a) - records.findIndex((r) => r.id === b)))
    // the matrix's write: [key][write][MATRIX], then each row's target (u16) and its 11 cells
    const matrix = records.find((r) => r.id === record('SYS_ACTIONS_ID_POWER_RESPONSES'))!.frames[0]!
    expect([...matrix.subarray(3)]).toEqual(rows.flatMap((r) => [r.target & 0xff, r.target >> 8, ...r.cells]))
  })

  it('writes nothing for what the project does not keep', () => {
    expect(settingsRecords(catalog, emptySettings())).toEqual([])
  })
})
