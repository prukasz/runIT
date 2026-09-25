import { packPacket, PacketPackError, unpackStruct } from '../../backend/packetPack'
import type { PacketField, PacketScalarType, PacketStructValues, PacketValue } from '../../backend/packetPack'
import type { GeneratedChoice, GeneratedContractsFile, GeneratedField, GeneratedLayout, GeneratedPacket, GeneratedSettingsFile } from './generatedTypes'

/** One field of a command or response, with what a form needs to edit or show it. */
export interface CommandFieldInfo {
  readonly name: string
  readonly label: string
  readonly type: string
  readonly kind: 'number' | 'array' | 'text'
  readonly required: boolean
  /** Value sent when an optional field is left out. */
  readonly fallback: number
  readonly arrayLength?: number
  readonly choices?: readonly GeneratedChoice[]
  readonly enumRef?: string
  readonly unit?: string
  readonly note?: string
  readonly min?: number
  readonly max?: number
}

export interface CommandLayout {
  readonly fields: readonly CommandFieldInfo[]
  /** Wire layout for packetPack. */
  readonly wire: readonly PacketField[]
}

export interface CommandGroup {
  /** Catalog ID (`system`, `events`) or settings tag (`ble`, `power` …). */
  readonly id: string
  readonly source: 'contracts' | 'settings'
  readonly title: string
  readonly description: string
  readonly classHeader: number
}

export interface CommandDescriptor {
  /** Packet struct name, e.g. `packet_sys_io_get_level_t`. */
  readonly id: string
  /** `sys_io_get_level` */
  readonly name: string
  readonly group: CommandGroup
  readonly classHeader: number
  readonly packetHeader: number
  readonly request: CommandLayout
  /** Present when an OK answer carries data. */
  readonly response?: CommandLayout
}

export interface CommandCatalog {
  readonly groups: readonly CommandGroup[]
  readonly commands: readonly CommandDescriptor[]
  get(id: string): CommandDescriptor | undefined
}

export class DescriptorError extends Error {
  constructor(message: string) {
    super(message)
    this.name = 'DescriptorError'
  }
}

const SCALAR_TYPES: ReadonlySet<string> = new Set<PacketScalarType>([
  'uint8_t', 'int8_t', 'uint16_t', 'int16_t', 'uint32_t', 'int32_t', 'uint64_t', 'int64_t', 'float', 'double', 'bool', '_Bool',
])

export const parseByte = (text: string, where: string): number => {
  const value = Number.parseInt(text, 16)
  if (!/^0x[0-9a-f]{1,2}$/i.test(text) || value > 0xff) throw new DescriptorError(`${where}: '${text}' is not a one-byte hex header.`)
  return value
}

const toFieldInfo = (name: string, field: GeneratedField, where: string): CommandFieldInfo => {
  const text = field.type === 'char' && field.flexible_array === true
  if (!text && !SCALAR_TYPES.has(field.type)) throw new DescriptorError(`${where}.${name}: unsupported type '${field.type}'.`)
  if (text && field.encoding !== undefined && field.encoding !== 'utf-8') throw new DescriptorError(`${where}.${name}: unsupported encoding '${field.encoding}'.`)
  return {
    name,
    label: field.alias ?? name,
    type: field.type,
    kind: text ? 'text' : field.array_len !== undefined ? 'array' : 'number',
    required: field.required ?? true,
    fallback: field.sentinel ?? 0,
    arrayLength: field.array_len,
    choices: field.one_of,
    enumRef: field.enum_ref,
    unit: field.unit,
    note: field.note,
    min: field.min,
    max: field.max,
  }
}

const toWireField = (info: CommandFieldInfo): PacketField => {
  if (info.kind === 'text') return { kind: 'text', name: info.name, nulTerminate: true }
  return { kind: 'scalar', name: info.name, type: info.type as PacketScalarType, length: info.arrayLength }
}

const toLayout = (layout: GeneratedLayout, where: string): CommandLayout => {
  const fields = layout.field_order.map((name) => {
    const field = layout.fields[name]
    if (!field) throw new DescriptorError(`${where}: field_order names '${name}', which has no field entry.`)
    return toFieldInfo(name, field, where)
  })
  const textIndex = fields.findIndex((field) => field.kind === 'text')
  if (textIndex >= 0 && textIndex !== fields.length - 1) throw new DescriptorError(`${where}: a flexible text field must be last.`)
  return { fields, wire: fields.map(toWireField) }
}

const toDescriptor = (packet: GeneratedPacket, group: CommandGroup): CommandDescriptor => ({
  id: packet.id,
  name: packet.id.replace(/^packet_/, '').replace(/_t$/, ''),
  group,
  classHeader: group.classHeader,
  packetHeader: parseByte(packet.packet_header, packet.id),
  request: toLayout(packet, packet.id),
  response: packet.response ? toLayout(packet.response, `${packet.id} response`) : undefined,
})

/** Build the command catalog from the generated contracts and settings files. */
export const buildCommandCatalog = (contracts: GeneratedContractsFile, settings: GeneratedSettingsFile): CommandCatalog => {
  if (contracts.response_stream.matching !== 'seq') throw new DescriptorError(`Unsupported response matching '${contracts.response_stream.matching}'.`)

  const groups: CommandGroup[] = []
  const commands: CommandDescriptor[] = []
  const add = (group: CommandGroup, packets: readonly GeneratedPacket[]): void => {
    groups.push(group)
    packets.forEach((packet) => commands.push(toDescriptor(packet, group)))
  }
  contracts.catalogs.forEach((catalog) => add(
    { id: catalog.id, source: 'contracts', title: catalog.title, description: catalog.description, classHeader: parseByte(catalog.class_header, catalog.id) },
    catalog.contracts,
  ))
  settings.settings.forEach((entry) => add(
    { id: entry.tag, source: 'settings', title: entry.title, description: entry.description, classHeader: parseByte(entry.class_header, entry.tag) },
    entry.packets,
  ))

  const byId = new Map<string, CommandDescriptor>()
  const byHeader = new Map<number, string>()
  for (const command of commands) {
    if (byId.has(command.id)) throw new DescriptorError(`Command '${command.id}' is defined twice.`)
    const header = (command.classHeader << 8) | command.packetHeader
    const clash = byHeader.get(header)
    if (clash) throw new DescriptorError(`'${command.id}' and '${clash}' share class/packet ${command.classHeader}/${command.packetHeader}.`)
    byId.set(command.id, command)
    byHeader.set(header, command.id)
  }
  return { groups, commands, get: (id) => byId.get(id) }
}

/**
 * `[class][packet][payload]` for a command (the client adds the seq byte).
 * Every field goes on the wire: a missing optional field is sent as its
 * sentinel or 0, a missing required one is an error.
 */
export const packCommand = (command: CommandDescriptor, values: PacketStructValues): Uint8Array => {
  const known = new Set(command.request.fields.map((field) => field.name))
  const unknown = Object.keys(values).filter((name) => !known.has(name))
  if (unknown.length) throw new PacketPackError(`${command.id}: unknown fields ${unknown.join(', ')}.`)

  const filled: Record<string, PacketValue> = {}
  for (const field of command.request.fields) {
    const value = values[field.name]
    if (value !== undefined) filled[field.name] = value
    else if (field.required) throw new PacketPackError(`${command.id}: required field '${field.name}' is missing.`)
    else if (field.kind === 'text') filled[field.name] = ''
    else if (field.kind === 'array') filled[field.name] = new Array<number>(field.arrayLength ?? 0).fill(field.fallback)
    else filled[field.name] = field.fallback
  }
  return packPacket({ name: command.id, classHeader: command.classHeader, functionHeader: command.packetHeader, fields: command.request.wire }, filled)
}

export interface DecodedResponseData {
  readonly values: PacketStructValues
  /** Bytes after the known layout (a newer firmware may append fields). */
  readonly extra: Uint8Array
}

/** Undefined when the command has no response layout. Throws when the data is shorter than the layout. */
export const decodeResponseData = (command: CommandDescriptor, data: Uint8Array): DecodedResponseData | undefined => {
  if (!command.response) return undefined
  const { values, consumed } = unpackStruct(command.response.wire, data)
  return { values, extra: data.slice(consumed) }
}
