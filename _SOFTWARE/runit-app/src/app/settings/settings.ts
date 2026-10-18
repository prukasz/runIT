import { encodeFields } from '@backend'
import type { CatalogEntry, CommandDef, Decoded, FieldInput, FieldValues, Layout, ServiceCatalog, WireField } from '@backend'

/*
 * The board's settings, as the service descriptors (`generated/services/contract_*.json`) mark them. Nothing here names a
 * service's fields: the descriptors give the commands, the layouts, the labels, the help and the records.
 *
 *   form    a command that writes a struct and names the command that reads it back (`read_by`: no arguments, answers a
 *           struct holding every field the write takes). Its fields make the form.
 *   table   a command that writes a whole table as bytes and names a read that answers its rows (an array of structs: the
 *           row's target first, then an array field whose elements are the columns, `index_enum` naming them).
 *   record  a service whose settings are one record (`record` on the service: the GATT table, the data streams). Its
 *           editor is the app's (gatt.ts, streams.ts); the commands and their fields still come from the descriptor.
 *
 * A form or table with `record` is the project's (written as that settings record, applied at every start); one without is
 * the board's own (it keeps it itself): written at once, never stored in the project.
 */

/** A setting: the command that writes it, the one that reads it back, and the record the project keeps it in. */
export interface SettingGroup {
  /** `<service>.<write command>`: the group's key. */
  readonly key: string
  readonly service: string
  readonly write: CommandDef
  readonly read: CommandDef
  /** The write's arguments: the form. */
  readonly layout: Layout
  readonly recordId?: number
}

/** A settings table: rows of a target and one value per column, written whole. */
export interface SettingTable {
  readonly key: string
  readonly service: string
  readonly write: CommandDef
  readonly read: CommandDef
  /** The row's target (a device, or the board). */
  readonly target: WireField
  /** The array field of the row: one element per column. */
  readonly cells: WireField
  readonly recordId?: number
}

/** The editor of a service kept as one record, by what the service does. */
export type RecordEditor = 'gatt' | 'streams'

/** The settings of one service descriptor: a tab of the Project view. */
export interface SettingSection {
  readonly service: CatalogEntry
  /** The descriptor's alias without its parenthesised second name: `Logging settings`. */
  readonly title: string
  readonly groups: readonly SettingGroup[]
  readonly tables: readonly SettingTable[]
  /** A service kept as one record, and its editor. */
  readonly record?: { readonly id: number; readonly editor: RecordEditor }
}

/** The services whose record the app edits with its own editor, by the descriptor's id. */
export const RECORD_EDITORS: Readonly<Record<string, RecordEditor>> = { BLE: 'gatt', CONNECTOR: 'streams' }

/** The fields a command answers when it answers one struct (or each element of a list of them). */
const answerFields = (command: CommandDef, array: boolean): readonly WireField[] | undefined =>
  command.returns?.kind === 'struct' && command.returns.array === array ? command.returns.layout.fields : undefined

const takesNothing = (command: CommandDef): boolean => (command.args?.fields.length ?? 0) === 0

/**
 * Every setting the catalog's services mark, by service in the catalog's order. A mark that does not fit (an unknown read, a
 * read with arguments, one that does not answer what the write takes) is left out and named in `problems`.
 */
export const settingSections = (catalog: ServiceCatalog, problems: string[] = []): SettingSection[] =>
  [...catalog.services.values()].flatMap((service) => {
    const groups: SettingGroup[] = []
    const tables: SettingTable[] = []
    for (const write of service.commands.values()) {
      if (!write.readBy) continue
      const key = `${service.id}.${write.id}`
      const read = service.commands.get(write.readBy)
      const record = write.recordId !== undefined ? { recordId: write.recordId } : {}
      const struct = read && takesNothing(read) ? answerFields(read, false) : undefined
      const rows = read && takesNothing(read) ? answerFields(read, true) : undefined
      if (read && struct && write.args && !write.args.variable && write.args.fields.every((f) => struct.some((a) => a.id === f.id))) {
        groups.push({ key, service: service.id, write, read, layout: write.args, ...record })
      } else if (read && rows && rows.length === 2 && rows[1]!.indexEnum && write.args?.variable && write.args.fields.length === 1) {
        tables.push({ key, service: service.id, write, read, target: rows[0]!, cells: rows[1]!, ...record })
      } else {
        problems.push(`${key}: read_by ${write.readBy} does not read back what it writes.`)
      }
    }
    const editor = RECORD_EDITORS[service.id]
    const record = service.recordId !== undefined && editor ? { id: service.recordId, editor } : undefined
    if (service.recordId !== undefined && !editor) problems.push(`${service.id}: record ${service.recordId} has no editor in the app.`)
    if (groups.length === 0 && tables.length === 0 && !record) return []
    return [{ service, title: service.label.replace(/\s*\([^)]*\)\s*$/, ''), groups, tables, ...(record ? { record } : {}) }]
  })

/** What a group shows of its read's answer: the fields it writes, nothing else. */
export const groupValues = (group: SettingGroup, answer: Decoded | undefined): FieldValues => {
  if (!answer || typeof answer !== 'object' || Array.isArray(answer) || answer instanceof Uint8Array) return {}
  const record = answer as Readonly<Record<string, unknown>>
  return Object.fromEntries(group.layout.fields.filter((f) => f.id in record).map((f) => [f.id, record[f.id] as FieldInput]))
}

/** A table's rows from its read's answer. */
export const tableRows = (table: SettingTable, answer: Decoded | undefined): { target: number; cells: number[] }[] =>
  (Array.isArray(answer) ? answer : []).flatMap((row) => {
    if (!row || typeof row !== 'object' || Array.isArray(row) || row instanceof Uint8Array) return []
    const record = row as Readonly<Record<string, unknown>>
    const cells = record[table.cells.id]
    return [{ target: Number(record[table.target.id]), cells: Array.isArray(cells) ? cells.map(Number) : [] }]
  })

/** The bytes a table's write takes: every row's cells, in the rows' order, without the targets. */
export const tableBytes = (rows: readonly { readonly cells: readonly number[] }[]): number[] => rows.flatMap((r) => [...r.cells])

/** A setting's name from its write's label, without the verb: `Set the logging options` → `Logging options`. */
export const settingTitle = (write: CommandDef): string => {
  const name = write.label.replace(/^set\s+(the\s+)?/i, '')
  return name.charAt(0).toUpperCase() + name.slice(1)
}

/** Two values of a form say the same to the board: they encode to the same bytes (a choice by its label or its value alike). */
export const sameValues = (layout: Layout, a: FieldValues, b: FieldValues): boolean => {
  try {
    const [x, y] = [encodeFields(layout, a), encodeFields(layout, b)]
    return x.length === y.length && x.every((v, i) => v === y[i])
  } catch {
    return false
  }
}
