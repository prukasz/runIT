import { describe, expect, it } from 'vitest'
import { bundledEnums, bundledServiceCatalog, encodeFields, serviceCall } from '../../src/backend'
import type { DataStream, GattService } from '../../src/app/project/projectFile'
import { emptySettings } from '../../src/app/project/projectFile'
import { decodeGattList, gattApply, gattFromRecord, gattRecord } from '../../src/app/settings/gatt'
import { groupValues, sameValues, settingSections, tableBytes } from '../../src/app/settings/settings'
import { settingsFromRecords, settingsRecords } from '../../src/app/settings/settingsRecords'
import { decodeStreamsInfo, streamsApply, streamsFromRecord, streamsRecord } from '../../src/app/settings/streams'

const catalog = bundledServiceCatalog()
const problems: string[] = []
const sections = settingSections(catalog, problems)
const group = (key: string) => sections.flatMap((s) => s.groups).find((g) => g.key === key)
const table = (key: string) => sections.flatMap((s) => s.tables).find((t) => t.key === key)
const record = (name: string) => bundledEnums().settings_record!.members.find((m) => m.name === name)!.value

describe('the board settings from the service descriptors', () => {
  it('the time the app gives the board is a SYS command in the descriptors, not a setting', () => {
    const set = serviceCall(catalog, 'SYS', 'TIME_SET', { unix_ms: 1_760_000_000_123 })
    expect([...set.request.payload!.subarray(0, 8)]).toEqual([...new Uint8Array(new BigUint64Array([1_760_000_000_123n]).buffer)])
    expect(group('SYS.TIME_SET')).toBeUndefined()
    expect(group('SYS.BYE')).toBeUndefined()
  })

  it('takes the writes the descriptors mark with read_by, each read answering its fields, one section per service', () => {
    expect(problems).toEqual([])
    expect(group('LOG.SET')?.read.id).toBe('GET')
    expect(group('POWER_MANAGER.SET_CONFIG')?.read.id).toBe('GET_CONFIG')
    expect(group('POWER_MANAGER.SET_PSU')?.read.id).toBe('GET_PSU')
    expect(group('POWER_MANAGER.SET_BATTERY')?.read.id).toBe('GET_BATTERY')
    // The read may answer more than the write takes: autostart is one field of the project's INFO.
    expect(group('PROJECT.OPTIONS')?.read.id).toBe('INFO')
    expect(group('SYS.DEADMAN_SET')?.read.id).toBe('DEADMAN_GET')
    expect(sections.find((s) => s.service.id === 'LOG')?.title).toBe('Logging settings')
  })

  it('keeps a marked setting in its record; one without is the board\'s own', () => {
    expect(group('LOG.SET')?.recordId).toBe(record('SYS_ACTIONS_ID_LOG'))
    expect(group('POWER_MANAGER.SET_PSU')?.recordId).toBe(record('SYS_ACTIONS_ID_POWER_PSU'))
    expect(group('PROJECT.OPTIONS')?.recordId).toBeUndefined()
    // The dead-man is runtime only: the project never keeps it, so a board that restarts runs offline.
    expect(group('SYS.DEADMAN_SET')?.recordId).toBeUndefined()
    expect(sections.find((s) => s.service.id === 'BLE')?.record).toEqual({ id: record('SYS_ACTIONS_ID_BLE'), editor: 'gatt' })
    expect(sections.find((s) => s.service.id === 'CONNECTOR')?.record).toEqual({ id: record('SYS_ACTIONS_ID_CONN'), editor: 'streams' })
  })

  it('reads the response matrix as a table: a target per row, a column per power event kind', () => {
    const matrix = table('POWER_MANAGER.SET_RESPONSES')!
    expect(matrix.read.id).toBe('GET_MATRIX')
    expect(matrix.cells.indexEnum).toBe('sys_power_kind_e')
    expect(matrix.cells.choices?.length).toBe(bundledEnums().sys_power_response_e!.members.length)
    expect(matrix.recordId).toBe(record('SYS_ACTIONS_ID_POWER_RESPONSES'))
  })

  it('leaves out every write without the mark: nothing that only looks like a setting is ever sent as a read', () => {
    expect(group('ACTIONS.RECORD_START')).toBeUndefined()
    expect(group('VM.SPEED')).toBeUndefined()
  })

  it('shows only the written fields of an answer, and compares values as the board gets them', () => {
    const options = group('PROJECT.OPTIONS')!
    const values = groupValues(options, { stored: true, autostart: true, bank: 0, crc32: 1, bytes: 10 })
    expect(values).toEqual({ autostart: true })
    expect([...encodeFields(options.layout, values)]).toEqual([1])
    const log = group('LOG.SET')!
    const board = groupValues(log, { level: 3, mirror_serial: true, trace_errors: false, send_errors: true, send_logs: false })
    expect(() => serviceCall(catalog, 'LOG', 'SET', board)).not.toThrow()
    expect(sameValues(log.layout, board, { ...board, level: 3 })).toBe(true)
    expect(sameValues(log.layout, board, { ...board, send_logs: true })).toBe(false)
  })
})

describe('the settings the project keeps, as records of the stored project', () => {
  const gatt: GattService[] = [{ uuid: 0xee00, primary: true, characteristics: [{ uuid: 0xee01, name: 'temp', write: false, indicate: false, notify: true, framed: false, tx: 96, rx: 0 }] }]
  const streams: DataStream[] = [
    { id: 0, name: 'logs', stream: 3, system: true, suspended: false, bindings: [{ provider: 2, endpoint: 0xffe3 }] },
    { id: 4, name: 'mine', stream: 0x10, system: false, suspended: true, bindings: [{ provider: 1, endpoint: 0 }] },
  ]

  it('builds the GATT record as SYS_BLE.MD shows it: the service, its characteristic, APPLY', () => {
    const frames = gattRecord(catalog, gatt).map((f) => [...f])
    expect(frames[0]).toEqual([0x03, 0x01, 0x00, 0xee, 0x01])
    expect(frames[1]).toEqual([0x03, 0x03, 0x00, 0xee, 0x01, 0xee, 0, 0, 1, 0, 0x60, 0, 0, 0, 0, 0, 0, 0, 0x74, 0x65, 0x6d, 0x70, 0])
    expect(frames[2]).toEqual([0x03, 0x05])
    expect(gattFromRecord(catalog, gattRecord(catalog, gatt))).toEqual(gatt)
  })

  it('reads the GATT table of LIST, and rebuilds only the user services', () => {
    const list = Uint8Array.of(2, 0xe0, 0xff, 0b11, 0, 0x00, 0xee, 0b01, 1, 0x01, 0xee, 0b100, 0x60, 0, 0, 0, 0, 0, 0, 0, 0x74, 0x65, 0x6d, 0x70, 0)
    const live = decodeGattList(list)
    expect(live[0]).toMatchObject({ uuid: 0xffe0, locked: true })
    expect(live[1]).toMatchObject({ uuid: 0xee00, locked: false, characteristics: [{ uuid: 0xee01, name: 'temp', notify: true, tx: 96 }] })
    expect(gattApply(catalog, live, gatt).map((c) => c.command.id)).toEqual(['SVC_REMOVE', 'SVC_CREATE', 'CHAR_CREATE', 'APPLY'])
  })

  it('builds the streams record: the user stream created, bindings set, the transports not used unbound, a pause', () => {
    const ids = streamsRecord(catalog, streams).map((f) => f[1])
    const op = (id: string) => catalog.command(catalog.service('CONNECTOR'), id).op
    expect(ids).toEqual([op('BIND'), op('UNBIND'), op('CREATE'), op('BIND'), op('UNBIND'), op('SUSPEND')])
    const info = Uint8Array.of(1, 0, 3, 1, 2, 1, 0, 0, 0, 0, 2, 0xe3, 0xff, 0, 0, 0x6c, 0x6f, 0x67, 0x73, 0)
    const live = decodeStreamsInfo(info)
    expect(live).toEqual([{ id: 0, stream: 3, name: 'logs', system: true, suspended: false, bindings: [{ provider: 1, endpoint: 0 }, { provider: 2, endpoint: 0xffe3 }] }])
    expect(streamsFromRecord(catalog, streamsRecord(catalog, streams), live)).toEqual(streams)
    expect(streamsApply(catalog, live, streams).map((c) => c.command.id)).toEqual(['UNBIND', 'CREATE', 'BIND', 'SUSPEND'])
  })

  it('writes each kept setting as its record and reads them back over what the board has', () => {
    const rows = [{ target: 0xffff, cells: Array.from({ length: 11 }, () => 0) }, { target: 10, cells: Array.from({ length: 11 }, (_, i) => (i === 3 ? 1 : 0)) }]
    const settings = {
      ...emptySettings(),
      values: { 'LOG.SET': { level: 2, mirror_serial: true, trace_errors: false, send_errors: true, send_logs: false } },
      tables: { 'POWER_MANAGER.SET_RESPONSES': rows },
      gatt, streams,
    }
    const records = settingsRecords(catalog, settings)
    expect(records.map((r) => r.id)).toEqual([record('SYS_ACTIONS_ID_LOG'), record('SYS_ACTIONS_ID_POWER_RESPONSES'), record('SYS_ACTIONS_ID_BLE'), record('SYS_ACTIONS_ID_CONN')].sort((a, b) => records.findIndex((r) => r.id === a) - records.findIndex((r) => r.id === b)))
    expect([...records.find((r) => r.id === record('SYS_ACTIONS_ID_POWER_RESPONSES'))!.frames[0]!.subarray(2)]).toEqual(tableBytes(rows))
    const back = settingsFromRecords(catalog, records, { tables: { 'POWER_MANAGER.SET_RESPONSES': rows.map((r) => ({ ...r, cells: [] })) }, streams: [streams[0]!] })
    expect(sameValues(group('LOG.SET')!.layout, back.values['LOG.SET']!, settings.values['LOG.SET'])).toBe(true)
    expect(back.tables['POWER_MANAGER.SET_RESPONSES']).toEqual(rows)
    expect(back.gatt).toEqual(gatt)
    expect(back.streams).toEqual(streams)
  })

  it('writes nothing for what the project does not keep', () => {
    expect(settingsRecords(catalog, emptySettings())).toEqual([])
  })
})
