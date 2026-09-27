import type { PacketScalarType } from '../../backend/packetPack'
import { DescriptorError, parseByte } from './commandCatalog'
import type { GeneratedVmModelField, GeneratedVmModelFile, GeneratedVmProgramFile } from './generatedTypes'

/*
 * The VM program format (vm-program.generated.json) and object header
 * (vm-model.generated.json): packet bytes, object types, the wire layout of
 * vm_obj_head_t, limits and arena sizes. The compiler builds packets from it.
 */

export interface VmObjectType {
  /** The symbol without `VM_OBJ_`: `U8`, `F`, `PTR` … — the name project files use. */
  readonly key: string
  readonly symbol: string
  readonly value: number
  readonly alias: string
  /** Bytes per element in the arena (payload_size = memory width x elements). */
  readonly memoryWidth: number
  /** Bytes per element in 0x43 data. */
  readonly wireWidth: number
  readonly wireType: PacketScalarType
}

/** Wire shape of one VM packet (vm-program packets[]). */
export interface VmPacketWire {
  readonly header: number
  /** Bytes of the fixed part of one record (a batched packet repeats it after the count). */
  readonly recordSize: number
  /** Batched packets: most records per packet and the width of the count before them. */
  readonly batch?: { readonly max: number; readonly countBytes: number }
}

/** One header field: a whole u16, or a bit field inside one byte. */
export interface VmHeadField {
  readonly offset: number
  readonly bit?: number
  readonly width?: number
}

export interface VmCatalog {
  readonly classHeader: number
  readonly packets: {
    readonly reset: number
    readonly open: number
    readonly addObjects: number
    readonly setData: number
    readonly addAccessors: number
    readonly addBlock: number
    readonly subscribe: number
    readonly exec: number
  }
  /** Record size and batch limits per packet, same keys as `packets`. */
  readonly wire: { readonly [K in keyof VmCatalog['packets']]: VmPacketWire }
  readonly types: readonly VmObjectType[]
  readonly type: (key: string) => VmObjectType | undefined
  readonly ptrType: VmObjectType
  /** vm_obj_head_t on the wire; fields by path: `payload_size`, `d.obj_t`, `f.mutable` … */
  readonly head: { readonly size: number; readonly fields: ReadonlyMap<string, VmHeadField> }
  readonly nameMax: number
  /** Largest payload_size (its C type in vm_obj_head_t): an object holds at most payloadMax / memory width elements. */
  readonly payloadMax: number
  /** Most elements an object of this type holds. */
  readonly maxElements: (type: VmObjectType) => number
  readonly idNone: number
  /** Heap object IDs carry this bit; program object IDs stay below it. */
  readonly dynBit: number
  readonly arena: { readonly alignment: number; readonly pointer: number; readonly objectHead: number; readonly maxBytes: number }
  readonly retainMaxBytes: number
  /** vm_exec_command_e (0x48) by member name without `VM_EXEC_`: `NORMAL_MODE`, `PAUSE`, `RESET` … */
  readonly execCommands: ReadonlyMap<string, number>
  /** Device → app on the telemetry stream: [stream][class][packet][u8 count][records]. */
  readonly telemetry: { readonly stream: number; readonly classHeader: number; readonly describe: number; readonly values: number }
}

const PACKET_SYMBOLS = {
  reset: 'HEADER_packet_vm_reset',
  open: 'HEADER_packet_vm_open',
  addObjects: 'HEADER_packet_vm_add_objs',
  setData: 'HEADER_packet_vm_set_data',
  addAccessors: 'HEADER_packet_vm_add_acc',
  addBlock: 'HEADER_packet_vm_add_block',
  subscribe: 'HEADER_packet_vm_subscribe',
  exec: 'HEADER_packet_vm_exec',
} as const

const HEAD_FIELDS = ['payload_size', 'd.obj_t', 'd.name_size', 'f.mutable', 'f.upd_resetable', 'f.tagged', 'f.retentive', 'f.usr_protected'] as const

const WIRE_TYPES: Readonly<Record<string, PacketScalarType>> = { uint8_t: 'uint8_t', uint16_t: 'uint16_t', uint32_t: 'uint32_t', int32_t: 'int32_t', float: 'float' }

const UNSIGNED_MAX: Readonly<Record<string, number>> = { uint8_t: 0xff, uint16_t: 0xffff, uint32_t: 0xffffffff }

const COUNT_BYTES: Readonly<Record<string, number>> = { uint8_t: 1, uint16_t: 2 }

const need = <T>(value: T | undefined, what: string): T => {
  if (value === undefined) throw new DescriptorError(`VM descriptors lack ${what}.`)
  return value
}

const flattenHead = (fields: readonly GeneratedVmModelField[], prefix: string, out: Map<string, VmHeadField>): void => {
  for (const field of fields) {
    const path = `${prefix}${field.name}`
    if (field.kind === 'struct') {
      flattenHead(field.fields ?? [], `${path}.`, out)
      continue
    }
    const offset = Number(field.annotations?.wire_offset)
    if (!Number.isInteger(offset)) continue
    const bit = field.annotations?.wire_bit_offset
    out.set(path, bit === undefined ? { offset } : { offset, bit: Number(bit), width: field.bit_width ?? 1 })
  }
}

export const buildVmCatalog = (program: GeneratedVmProgramFile, model: GeneratedVmModelFile): VmCatalog => {
  const entryOf = (symbol: string) => need(program.packets.find((entry) => entry.symbol === symbol), `packet ${symbol}`)
  const packets = Object.fromEntries(Object.entries(PACKET_SYMBOLS).map(([key, symbol]) => [key, parseByte(entryOf(symbol).packet_header, symbol)])) as VmCatalog['packets']
  const wire = Object.fromEntries(Object.entries(PACKET_SYMBOLS).map(([key, symbol]) => {
    const entry = entryOf(symbol)
    const batch = entry.batch && { max: entry.batch.max, countBytes: need(COUNT_BYTES[entry.batch.count_type], `a count width for ${entry.batch.count_type} (${symbol})`) }
    return [key, { header: parseByte(entry.packet_header, symbol), recordSize: entry.record.size, ...(batch ? { batch } : {}) }]
  })) as VmCatalog['wire']

  const types = program.types.map((entry): VmObjectType => ({
    key: entry.symbol.replace(/^VM_OBJ_/, ''),
    symbol: entry.symbol,
    value: entry.value,
    alias: entry.alias,
    memoryWidth: entry.memory_width,
    wireWidth: entry.wire_width,
    wireType: need(WIRE_TYPES[entry.wire_type], `a packer type for ${entry.wire_type} (${entry.symbol})`),
  }))
  const byKey = new Map(types.map((entry) => [entry.key, entry]))

  const headStruct = need(model.structures.find((entry) => entry.name === 'vm_obj_head_t'), 'vm_obj_head_t')
  const headFields = new Map<string, VmHeadField>()
  flattenHead(headStruct.fields, '', headFields)
  for (const path of HEAD_FIELDS) need(headFields.get(path), `vm_obj_head_t.${path}`)

  const payloadType = need(headStruct.fields.find((field) => field.name === 'payload_size')?.c_type, 'vm_obj_head_t.payload_size type')
  const payloadMax = need(UNSIGNED_MAX[payloadType], `the range of ${payloadType} (payload_size)`)
  const constant = (name: string) => need(program.constants[name], `constant ${name}`).value
  const limit = (name: string) => need(program.limits[name], `limit ${name}`).value
  const size = (name: string) => need(program.sizes[name], `size of ${name}`)
  const telemetryPacket = (symbol: string) => parseByte(need(program.telemetry.frames.find((entry) => entry.symbol === symbol), `telemetry frame ${symbol}`).packet_header, symbol)

  return {
    classHeader: parseByte(program.class_header, 'vm class_header'),
    packets,
    wire,
    types,
    type: (key) => byKey.get(key),
    ptrType: need(byKey.get('PTR'), 'type VM_OBJ_PTR'),
    head: { size: Number(need(headStruct.annotations?.wire_size, 'vm_obj_head_t wire_size')), fields: headFields },
    nameMax: constant('VM_OBJ_NAME_MAX'),
    payloadMax,
    maxElements: (type) => Math.floor(payloadMax / type.memoryWidth),
    idNone: constant('VM_OBJ_ID_NONE'),
    dynBit: constant('VM_OBJ_ID_DYN_BIT'),
    arena: { alignment: program.arena.alignment, pointer: size('void*'), objectHead: size('vm_obj_head_t'), maxBytes: limit('CONFIG_VM_STORE_MAX_POOL') },
    retainMaxBytes: limit('CONFIG_VM_RETAIN_MAX_BYTES'),
    execCommands: new Map(need(model.enums.vm_exec_command_e, 'enum vm_exec_command_e').members.map((member) => [member.name.replace(/^VM_EXEC_/, ''), member.value])),
    telemetry: {
      stream: parseByte(program.telemetry.stream, 'vm telemetry stream'),
      classHeader: parseByte(program.telemetry.class_header, 'vm telemetry class_header'),
      describe: telemetryPacket('CONFIG_TX_PACKET_HEADER_VM_DESCRIBE'),
      values: telemetryPacket('CONFIG_TX_PACKET_HEADER_VM_SET_DATA'),
    },
  }
}

/** Encode vm_obj_head_t from field values by path; fields left out are 0. */
export const encodeVmObjectHead = (catalog: VmCatalog, values: Readonly<Record<string, number>>): Uint8Array => {
  const bytes = new Uint8Array(catalog.head.size)
  const view = new DataView(bytes.buffer)
  for (const [path, value] of Object.entries(values)) {
    const field = catalog.head.fields.get(path)
    if (!field) throw new DescriptorError(`vm_obj_head_t has no field ${path}.`)
    if (field.bit === undefined) {
      view.setUint16(field.offset, value, true)
      continue
    }
    const mask = (1 << (field.width ?? 1)) - 1
    if (value < 0 || value > mask) throw new DescriptorError(`vm_obj_head_t.${path} = ${value} doesn't fit ${field.width} bit(s).`)
    bytes[field.offset] |= value << field.bit
  }
  return bytes
}

/** Reverse of encodeVmObjectHead: every published field by path. */
export const decodeVmObjectHead = (catalog: VmCatalog, bytes: Uint8Array): Readonly<Record<string, number>> => {
  if (bytes.byteLength < catalog.head.size) throw new DescriptorError(`vm_obj_head_t needs ${catalog.head.size} bytes, got ${bytes.byteLength}.`)
  const view = new DataView(bytes.buffer, bytes.byteOffset, catalog.head.size)
  const values: Record<string, number> = {}
  for (const [path, field] of catalog.head.fields) {
    values[path] = field.bit === undefined ? view.getUint16(field.offset, true) : (bytes[field.offset]! >> field.bit) & ((1 << (field.width ?? 1)) - 1)
  }
  return values
}
