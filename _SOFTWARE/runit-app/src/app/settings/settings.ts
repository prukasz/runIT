import { encodeFields } from '@backend'
import type { CatalogEntry, CommandDef, Decoded, FieldInput, FieldValues, Layout, ServiceCatalog, WireField } from '@backend'

/*
 * The board's settings, as the service descriptors (`generated/services/contract_*.json`) describe them. Nothing here names a
 * service's fields: the descriptors give the commands, the layouts, the labels, the help and the records.
 *
 * A setting is a command with a read and a write whose read takes no key (the mirror of a module's own setting: the read
 * answers what the write takes, CMD.MD). A command tagged `app` is one the app sets itself (the clock): no setting.
 *
 *   form    the write takes one struct, the read answers it. Its fields make the form.
 *   table   both sides are rows (`array`): the row's target first, then an array field whose elements are the columns
 *           (`index_enum` naming them). The write is matched by target: a row left out keeps its values.
 *   record  a service whose settings are one record (`record` on the service: the GATT table, the data streams). Its
 *           editor is the app's (gatt.ts, streams.ts); the commands and their fields still come from the descriptor.
 *
 * A form or table with `record` is the project's (written as that settings record, applied at every start); one without is
 * the board's own (it keeps it itself): written at once, never stored in the project.
 */

/** A setting: its command (read and written), and the record the project keeps it in. */
export interface SettingGroup {
  /** `<service>.<command>`: the group's key. */
  readonly key: string
  readonly service: string
  readonly command: CommandDef
  /** The write's arguments: the form. */
  readonly layout: Layout
  readonly recordId?: number
}

/** A settings table: rows of a target and one value per column. */
export interface SettingTable {
  readonly key: string
  readonly service: string
  readonly command: CommandDef
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

/** The fields the read of a command answers when it answers one struct (or each element of a list of them). */
const answerFields = (command: CommandDef, array: boolean): readonly WireField[] | undefined => {
  const returns = command.read?.returns
  return returns?.kind === 'struct' && returns.array === array ? returns.layout.fields : undefined
}

const sameFields = (a: readonly WireField[], b: readonly WireField[]): boolean => a.length === b.length && a.every((f, i) => f.id === b[i]!.id && f.offset === b[i]!.offset && f.size === b[i]!.size)

/**
 * Every setting of the catalog's services, by service in the catalog's order: each mirror whose read takes no key. One whose
 * read does not answer what its write takes is left out and named in `problems`.
 */
export const settingSections = (catalog: ServiceCatalog, problems: string[] = []): SettingSection[] =>
  [...catalog.services.values()].flatMap((service) => {
    const groups: SettingGroup[] = []
    const tables: SettingTable[] = []
    for (const command of service.commands.values()) {
      const { read, write } = command
      if (!read || !write || (read.args?.fields.length ?? 0) > 0 || command.tags.includes('app')) continue
      const key = `${service.id}.${command.id}`
      const record = command.recordId !== undefined ? { recordId: command.recordId } : {}
      const struct = answerFields(command, false)
      const rows = answerFields(command, true)
      if (struct && write.args && !write.args.array && !write.args.variable && write.args.fields.every((f) => struct.some((a) => a.id === f.id))) {
        groups.push({ key, service: service.id, command, layout: write.args, ...record })
      } else if (rows && rows.length === 2 && rows[1]!.indexEnum && write.args?.array && sameFields(write.args.fields, rows)) {
        tables.push({ key, service: service.id, command, target: rows[0]!, cells: rows[1]!, ...record })
      } else {
        problems.push(`${key}: its read does not answer what its write takes.`)
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

/** The rows a table's write takes: each row's target and cells, by the row's field ids. */
export const tableArgs = (table: SettingTable, rows: readonly { readonly target: number; readonly cells: readonly number[] }[]): FieldValues[] =>
  rows.map((r) => ({ [table.target.id]: r.target, [table.cells.id]: [...r.cells] }))

/** Two tables say the same to the board: the same rows, targets and cells, in the same order. */
export const sameRows = (a: readonly { readonly target: number; readonly cells: readonly number[] }[], b: readonly { readonly target: number; readonly cells: readonly number[] }[]): boolean =>
  JSON.stringify(a.map((r) => [r.target, r.cells])) === JSON.stringify(b.map((r) => [r.target, r.cells]))

/** A setting's name: its command's label, a leading verb dropped (`Set the logging options` → `Logging options`). */
export const settingTitle = (command: CommandDef): string => {
  const name = command.label.replace(/^set\s+(the\s+)?/i, '')
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
