import { describe, expect, it } from 'vitest'
import { bundledEnums, bundledServiceCatalog, encodeFields, serviceCall } from '../../src/backend'
import type { DataStream, GattService } from '../../src/app/project/projectFile'
import { emptySettings } from '../../src/app/project/projectFile'
import { decodeGattList, gattApply, gattFromRecord, gattRecord } from '../../src/app/settings/gatt'
import { groupValues, sameValues, settingSections } from '../../src/app/settings/settings'
import { settingsFromRecords, settingsRecords } from '../../src/app/settings/settingsRecords'
import { decodeStreamsInfo, sameStreams, streamsApply, streamsFromRecord, streamsRecord } from '../../src/app/settings/streams'

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
    for (const key of ['LOG.CONFIG', 'POWER_MANAGER.CONFIG', 'POWER_MANAGER.PSU', 'POWER_MANAGER.BATTERY', 'POWER_MANAGER.PD', 'PROJECT.OPTIONS', 'SYS.DEADMAN']) {
      const g = group(key)
      expect(g, key).toBeDefined()
      expect(g!.command.read && g!.command.write, key).toBeTruthy()
    }
    expect(sections.find((s) => s.service.id === 'LOG')?.title).toBe('Logging settings')
  })

  it('a read with a key (I2C RAW: bus, address, register) is no setting', () => {
    expect(group('I2C.RAW')).toBeUndefined()
  })

  it('keeps a marked setting in its record; one without is the board\'s own', () => {
    expect(group('LOG.CONFIG')?.recordId).toBe(record('SYS_ACTIONS_ID_LOG'))
    expect(group('POWER_MANAGER.PSU')?.recordId).toBe(record('SYS_ACTIONS_ID_POWER_PSU'))
    expect(group('POWER_MANAGER.PD')?.recordId).toBe(record('SYS_ACTIONS_ID_POWER_PD'))
    expect(group('PROJECT.OPTIONS')?.recordId).toBeUndefined()
    // The dead-man is runtime only: the project never keeps it, so a board that restarts runs offline.
    expect(group('SYS.DEADMAN')?.recordId).toBeUndefined()
    expect(sections.find((s) => s.service.id === 'BLE')?.record).toEqual({ id: record('SYS_ACTIONS_ID_BLE'), editor: 'gatt' })
    expect(sections.find((s) => s.service.id === 'CONNECTOR')?.record).toEqual({ id: record('SYS_ACTIONS_ID_CONN'), editor: 'streams' })
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
    expect(group('ACTIONS.RECORD_START')).toBeUndefined()
    expect(group('VM.SPEED')).toBeUndefined()
  })

  it('shows only the written fields of an answer, and compares values as the board gets them', () => {
    const options = group('PROJECT.OPTIONS')!
    const values = groupValues(options, { boot: 2, other: 3 })
    expect(values).toEqual({ boot: 2 })
    expect([...encodeFields(options.layout, values)]).toEqual([2])
    const log = group('LOG.CONFIG')!
    const board = groupValues(log, { level: 3, mirror_serial: true, trace_errors: false, send_errors: true, send_logs: false })
    expect(() => serviceCall(catalog, 'LOG', 'CONFIG', board, 'write')).not.toThrow()
    expect(sameValues(log.layout, board, { ...board, level: 3 })).toBe(true)
    expect(sameValues(log.layout, board, { ...board, send_logs: true })).toBe(false)
  })
})

describe('the settings the project keeps, as records of the stored project', () => {
  const gatt: GattService[] = [{ uuid: 0xee00, primary: true, characteristics: [{ uuid: 0xee01, name: 'temp', write: false, indicate: false, notify: true, framed: false, tx: 96, rx: 0 }] }]
  const streams: DataStream[] = [
    { stream: 3, system: true, suspended: false, bindings: [{ provider: 2, endpoint: 0xffe3 }] },
    { stream: 0x10, name: 'mine', system: false, suspended: true, bindings: [{ provider: 1, endpoint: 0 }] },
  ]

  it('builds the GATT record as SYS_BLE.MD shows it: the service, its characteristic, APPLY', () => {
    const frames = gattRecord(catalog, gatt).map((f) => [...f])
    expect(frames[0]).toEqual([0x03, 0x01, 0x01, 0x00, 0xee, 0x01]) // [BLE key][write][SVC_CREATE][uuid u16][primary]
    expect(frames[1]).toEqual([0x03, 0x01, 0x03, 0x00, 0xee, 0x01, 0xee, 0, 0, 1, 0, 0x60, 0, 0, 0, 0, 0, 0, 0, 0x74, 0x65, 0x6d, 0x70, 0])
    expect(frames[2]).toEqual([0x03, 0x01, 0x05])
    expect(gattFromRecord(catalog, gattRecord(catalog, gatt))).toEqual(gatt)
  })

  it('reads the GATT table of LIST, and rebuilds only the user services', () => {
    const list = Uint8Array.of(2, 0xe0, 0xff, 0b11, 0, 0x00, 0xee, 0b01, 1, 0x01, 0xee, 0b100, 0x60, 0, 0, 0, 0, 0, 0, 0, 0x74, 0x65, 0x6d, 0x70, 0)
    const live = decodeGattList(list)
    expect(live[0]).toMatchObject({ uuid: 0xffe0, locked: true })
    expect(live[1]).toMatchObject({ uuid: 0xee00, locked: false, characteristics: [{ uuid: 0xee01, name: 'temp', notify: true, tx: 96 }] })
    expect(gattApply(catalog, live, gatt).map((c) => c.command.id)).toEqual(['SVC_REMOVE', 'SVC_CREATE', 'CHAR_CREATE', 'APPLY'])
  })

  it('builds the streams record: the user stream created, every binding dropped, then set, a pause', () => {
    const ids = streamsRecord(catalog, streams).map((f) => f[2])
    const op = (id: string) => catalog.command(catalog.service('CONNECTOR'), id).op
    expect(ids).toEqual([op('UNBIND'), op('BIND'), op('CREATE'), op('UNBIND'), op('BIND'), op('SUSPEND')])
    // LIST: one stream, 0x03, keeps nothing, system; one subscriber (0xf0); bindings UART 0 and BLE 0xFFE3
    const info = Uint8Array.of(1, 3, 0, 0, 0, 0, 1, 1, 2, 0xf0, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0, 2, 0xe3, 0xff, 0, 0, 2, 0, 0)
    const live = decodeStreamsInfo(info)
    expect(live).toEqual([{ stream: 3, system: true, suspended: false, bindings: [{ provider: 1, endpoint: 0 }, { provider: 2, endpoint: 0xffe3 }] }])
    // the board keeps no name: a stream read back from the record has none
    expect(streamsFromRecord(catalog, streamsRecord(catalog, streams), live)).toEqual(streams.map(({ name: _, ...s }) => s))
    expect(sameStreams(streamsFromRecord(catalog, streamsRecord(catalog, streams), live), streams)).toBe(true)
    expect(streamsApply(catalog, live, streams).map((c) => c.command.id)).toEqual(['UNBIND', 'CREATE', 'BIND', 'SUSPEND'])
  })

  it('writes each kept setting as its record and reads them back over what the board has', () => {
    const rows = [{ target: 0xffff, cells: Array.from({ length: 11 }, () => 0) }, { target: 10, cells: Array.from({ length: 11 }, (_, i) => (i === 3 ? 1 : 0)) }]
    const settings = {
      ...emptySettings(),
      values: { 'LOG.CONFIG': { level: 2, mirror_serial: true, trace_errors: false, send_errors: true, send_logs: false } },
      tables: { 'POWER_MANAGER.MATRIX': rows },
      gatt, streams,
    }
    const records = settingsRecords(catalog, settings)
    expect(records.map((r) => r.id)).toEqual([record('SYS_ACTIONS_ID_LOG'), record('SYS_ACTIONS_ID_POWER_RESPONSES'), record('SYS_ACTIONS_ID_BLE'), record('SYS_ACTIONS_ID_CONN')].sort((a, b) => records.findIndex((r) => r.id === a) - records.findIndex((r) => r.id === b)))
    // the matrix's write: [key][write][MATRIX], then each row's target (u16) and its 11 cells
    const matrix = records.find((r) => r.id === record('SYS_ACTIONS_ID_POWER_RESPONSES'))!.frames[0]!
    expect([...matrix.subarray(3)]).toEqual(rows.flatMap((r) => [r.target & 0xff, r.target >> 8, ...r.cells]))
    const back = settingsFromRecords(catalog, records, { streams: [streams[0]!] })
    expect(sameValues(group('LOG.CONFIG')!.layout, back.values['LOG.CONFIG']!, settings.values['LOG.CONFIG'])).toBe(true)
    expect(back.tables['POWER_MANAGER.MATRIX']).toEqual(rows)
    expect(back.gatt).toEqual(gatt)
    expect(back.streams).toEqual(streams.map(({ name: _, ...s }) => s)) // the board keeps no name
  })

  it('writes nothing for what the project does not keep', () => {
    expect(settingsRecords(catalog, emptySettings())).toEqual([])
  })
})
