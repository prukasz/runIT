import { decodeFields, requestFrame, serviceCall, Services } from '@backend'
import type { FieldValues, RunitLink, Section, ServiceCatalog } from '@backend'
import type { DataStream, ProjectSettings, SettingsRow } from '../project/projectFile'
import { gattFromRecord, gattRecord } from './gatt'
import { settingSections, tableBytes, tableRows } from './settings'
import type { SettingTable } from './settings'
import { decodeStreamsInfo, STREAMS, streamsFromRecord, streamsRecord } from './streams'

/*
 * The project's settings as records of the stored project (SYS_PROJECT.MD: the settings sections run first at every start),
 * both ways. A setting the project does not keep has no section: the board keeps its own.
 */

/** The settings sections of the stored project, in the order the descriptors give them. */
export const settingsRecords = (catalog: ServiceCatalog, settings: ProjectSettings): Section[] =>
  settingSections(catalog).flatMap((section): Section[] => [
    ...section.groups.flatMap((g): Section[] => {
      const values = settings.values[g.key]
      return g.recordId !== undefined && values ? [{ id: g.recordId, frames: [requestFrame(serviceCall(catalog, g.service, g.write.id, values).request)] }] : []
    }),
    ...section.tables.flatMap((t): Section[] => {
      const rows = settings.tables[t.key]
      return t.recordId !== undefined && rows && rows.length > 0 ? [{ id: t.recordId, frames: [requestFrame(serviceCall(catalog, t.service, t.write.id, { [t.write.args!.fields[0]!.id]: tableBytes(rows) }).request)] }] : []
    }),
    ...(section.record?.editor === 'gatt' && settings.gatt ? [{ id: section.record.id, frames: gattRecord(catalog, settings.gatt) }] : []),
    ...(section.record?.editor === 'streams' && settings.streams ? [{ id: section.record.id, frames: streamsRecord(catalog, settings.streams) }] : []),
  ].filter((s) => s.frames.length > 0))

/** What the board has now that a stored table or stream record is read over: the rows' targets, the streams. */
export interface LiveBase {
  readonly tables: Readonly<Record<string, readonly SettingsRow[]>>
  readonly streams?: readonly DataStream[]
}

/** What a stored table or stream record is read over, read from the board: every table's rows, the streams. */
export const readLiveBase = async (link: RunitLink, catalog: ServiceCatalog): Promise<LiveBase> => {
  const services = new Services(link, catalog)
  const tables: Record<string, SettingsRow[]> = {}
  let streams: DataStream[] | undefined
  for (const section of settingSections(catalog)) {
    for (const t of section.tables) tables[t.key] = tableRows(t, (await services.call(t.service, t.read.id)).value)
    if (section.record?.editor === 'streams') streams = decodeStreamsInfo((await services.call(STREAMS.service, STREAMS.info)).data)
  }
  return { tables, ...(streams ? { streams } : {}) }
}

/** The rows of a stored table: its bytes cut into rows of the column count, each given the target the board has in that row. */
const rowsOf = (table: SettingTable, bytes: Uint8Array, live: readonly SettingsRow[] | undefined): SettingsRow[] | undefined => {
  const width = table.cells.size ?? 0
  if (width === 0 || bytes.length % width !== 0 || !live || live.length !== bytes.length / width) return undefined
  return live.map((row, i) => ({ target: row.target, cells: [...bytes.subarray(i * width, (i + 1) * width)] }))
}

/** The settings the stored project's sections hold, read back with the descriptors' layouts; `notes` names what could not be. */
export const settingsFromRecords = (catalog: ServiceCatalog, sections: readonly Section[], live: LiveBase, notes: string[] = []): ProjectSettings => {
  const values: Record<string, FieldValues> = {}
  const tables: Record<string, SettingsRow[]> = {}
  let gatt: ProjectSettings['gatt']
  let streams: ProjectSettings['streams']
  for (const settings of settingSections(catalog)) {
    const key = catalog.service(settings.service.id).key
    for (const section of sections) {
      const frames = section.frames.filter((f) => f[0] === key)
      for (const g of settings.groups) {
        if (g.recordId !== section.id) continue
        const frame = frames.filter((f) => f[1] === g.write.op).at(-1)
        if (frame) values[g.key] = decodeFields(g.layout, frame.subarray(2)) as FieldValues
      }
      for (const t of settings.tables) {
        if (t.recordId !== section.id) continue
        const frame = frames.filter((f) => f[1] === t.write.op).at(-1)
        const rows = frame ? rowsOf(t, frame.subarray(2), live.tables[t.key]) : undefined
        if (rows) tables[t.key] = rows
        else if (frame) notes.push(`${t.write.label}: the stored table does not fit the board's rows; not read.`)
      }
      if (settings.record?.id === section.id && settings.record.editor === 'gatt') gatt = gattFromRecord(catalog, frames)
      if (settings.record?.id === section.id && settings.record.editor === 'streams') streams = streamsFromRecord(catalog, frames, live.streams ?? [])
    }
  }
  return { values, tables, ...(gatt ? { gatt } : {}), ...(streams ? { streams } : {}) }
}
