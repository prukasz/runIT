import { requestFrame, serviceCall } from '@backend'
import type { ServiceCatalog } from '@backend'
import type { Section } from '../devices/devices'
import type { ProjectSettings } from '../project/projectFile'
import { gattRecord } from './gatt'
import { settingSections, tableArgs } from './settings'

/*
 * The project's settings as records of the stored project (SYS_PROJECT.MD: the settings sections run first at every start),
 * both ways. A setting the project does not keep has no section: the board keeps its own.
 */

/** The settings sections of the stored project, in the order the descriptors give them. */
export const settingsRecords = (catalog: ServiceCatalog, settings: ProjectSettings): Section[] => {
  const sections = settingSections(catalog).flatMap((section): Section[] => [
    ...section.groups.flatMap((g): Section[] => {
      const values = settings.values[g.key]
      return g.recordId !== undefined && values ? [{ id: g.recordId, frames: [requestFrame(serviceCall(catalog, g.service, g.command.id, values, 'write').request)] }] : []
    }),
    ...section.tables.flatMap((t): Section[] => {
      const rows = settings.tables[t.key]
      return t.recordId !== undefined && rows && rows.length > 0 ? [{ id: t.recordId, frames: [requestFrame(serviceCall(catalog, t.service, t.command.id, tableArgs(t, rows), 'write').request)] }] : []
    }),
    ...(section.record?.editor === 'ble' && settings.gatt ? [{ id: section.record.id, frames: gattRecord(catalog, settings.gatt) }] : []),
  ].filter((s) => s.frames.length > 0))
  // The settings of one service kept as one record share its id: their frames are one section.
  return [...sections.reduce((byId, s) => byId.set(s.id, { id: s.id, frames: [...(byId.get(s.id)?.frames ?? []), ...s.frames] }), new Map<number, Section>()).values()]
}
