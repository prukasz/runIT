import { decodeFields, PayloadError, requestFrame, serviceCall, Services } from '@backend'
import type { FieldValues, RunitLink, Section, ServiceCatalog } from '@backend'
import type { DataStream, ProjectSettings, SettingsRow } from '../project/projectFile'
import { gattFromRecord, gattRecord } from './gatt'
import { settingSections, tableArgs } from './settings'
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
      return g.recordId !== undefined && values ? [{ id: g.recordId, frames: [requestFrame(serviceCall(catalog, g.service, g.command.id, values, 'write').request)] }] : []
    }),
    ...section.tables.flatMap((t): Section[] => {
      const rows = settings.tables[t.key]
      return t.recordId !== undefined && rows && rows.length > 0 ? [{ id: t.recordId, frames: [requestFrame(serviceCall(catalog, t.service, t.command.id, tableArgs(t, rows), 'write').request)] }] : []
    }),
    ...(section.record?.editor === 'gatt' && settings.gatt ? [{ id: section.record.id, frames: gattRecord(catalog, settings.gatt) }] : []),
    ...(section.record?.editor === 'streams' && settings.streams ? [{ id: section.record.id, frames: streamsRecord(catalog, settings.streams) }] : []),
  ].filter((s) => s.frames.length > 0))

/** What the board has now that a stored stream record is read over: the streams. */
export interface LiveBase {
  readonly streams?: readonly DataStream[]
}

/** What a stored stream record is read over, read from the board: the streams. */
export const readLiveBase = async (link: RunitLink, catalog: ServiceCatalog): Promise<LiveBase> => {
  const services = new Services(link, catalog)
  let streams: DataStream[] | undefined
  for (const section of settingSections(catalog)) {
    if (section.record?.editor === 'streams') streams = decodeStreamsInfo((await services.call(STREAMS.service, STREAMS.info)).data)
  }
  return streams ? { streams } : {}
}

/** The rows of a stored table: its write's rows, each a target and its cells; undefined when they are not whole rows. */
const rowsOf = (table: SettingTable, bytes: Uint8Array): SettingsRow[] | undefined => {
  const layout = table.command.write!.args!
  if (layout.fixed === 0 || bytes.length % layout.fixed !== 0) return undefined
  try {
    return Array.from({ length: bytes.length / layout.fixed }, (_, i) => {
      const row = decodeFields(layout, bytes.subarray(i * layout.fixed, (i + 1) * layout.fixed))
      const cells = row[table.cells.id]
      return { target: Number(row[table.target.id]), cells: Array.isArray(cells) ? cells.map(Number) : [] }
    })
  } catch (error) {
    if (error instanceof PayloadError) return undefined
    throw error
  }
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
      // a record holds writes: [key][dir][op][args]
      const frames = section.frames.filter((f) => f[0] === key && f[1] === catalog.dirs.write)
      for (const g of settings.groups) {
        if (g.recordId !== section.id) continue
        const frame = frames.filter((f) => f[2] === g.command.op).at(-1)
        if (frame) values[g.key] = decodeFields(g.layout, frame.subarray(3)) as FieldValues
      }
      for (const t of settings.tables) {
        if (t.recordId !== section.id) continue
        const frame = frames.filter((f) => f[2] === t.command.op).at(-1)
        const rows = frame ? rowsOf(t, frame.subarray(3)) : undefined
        if (rows) tables[t.key] = rows
        else if (frame) notes.push(`${t.command.label}: the stored table is not whole rows; not read.`)
      }
      if (settings.record?.id === section.id && settings.record.editor === 'gatt') gatt = gattFromRecord(catalog, frames)
      if (settings.record?.id === section.id && settings.record.editor === 'streams') streams = streamsFromRecord(catalog, frames, live.streams ?? [])
    }
  }
  return { values, tables, ...(gatt ? { gatt } : {}), ...(streams ? { streams } : {}) }
}
