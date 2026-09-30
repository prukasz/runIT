import type { ErrorCatalog, ErrorLevelInfo, ErrorOwnerInfo, ErrorPayloadField, ErrorPayloadType, ErrorTagInfo, ValueNames } from '../descriptors'
import { formatPrintf } from './printf'
import type { PrintfValue } from './printf'

export type ErrorFieldValue = number | bigint | readonly (number | bigint)[]

/** One node of an error chain, resolved against the error catalog. */
export interface ErrorNodeReport {
  readonly tagId: number
  readonly ownerId: number
  /** Undefined when the catalog doesn't know the ID (catalog older than the firmware). */
  readonly tag?: ErrorTagInfo
  readonly owner?: ErrorOwnerInfo
  readonly level?: ErrorLevelInfo
  readonly payload: Uint8Array
  /** Payload fields that fit in the received bytes. */
  readonly fields: Readonly<Record<string, ErrorFieldValue>>
  /** Names of the field values the catalogs know (`dev_id` → `INA3221`, `mode` → an IO mode …). */
  readonly labels: Readonly<Record<string, string>>
  /** Payload length differs from the catalog's payload size. */
  readonly payloadMismatch: boolean
  /** The firmware's description, rendered from the catalog; `name=value` fields when there is no template. */
  readonly message: string
}

export interface ErrorReport {
  readonly nodeCount: number
  /** Chain depth on the board; more than nodeCount means the chain was cut to fit the frame. */
  readonly depth: number
  readonly schemaId: number
  /** The firmware's error maps match the app's catalog. */
  readonly schemaMatches: boolean
  readonly truncated: boolean
  /** The board marked the chain corrupt or over-depth. */
  readonly corrupt: boolean
  /** Outermost error first, root cause last. */
  readonly nodes: readonly ErrorNodeReport[]
  /** Set when the frame ended early; the nodes before it are kept. */
  readonly malformed?: string
}

const SIZES: Readonly<Record<ErrorPayloadType, number>> = {
  uint8_t: 1, int8_t: 1, uint16_t: 2, int16_t: 2, uint32_t: 4, int32_t: 4, uint64_t: 8, int64_t: 8, float: 4, double: 8,
}

const readScalar = (view: DataView, offset: number, type: ErrorPayloadType): number | bigint => {
  switch (type) {
    case 'uint8_t': return view.getUint8(offset)
    case 'int8_t': return view.getInt8(offset)
    case 'uint16_t': return view.getUint16(offset, true)
    case 'int16_t': return view.getInt16(offset, true)
    case 'uint32_t': return view.getUint32(offset, true)
    case 'int32_t': return view.getInt32(offset, true)
    case 'uint64_t': return view.getBigUint64(offset, true)
    case 'int64_t': return view.getBigInt64(offset, true)
    case 'float': return view.getFloat32(offset, true)
    case 'double': return view.getFloat64(offset, true)
  }
}

const readFields = (fields: readonly ErrorPayloadField[], payload: Uint8Array): Record<string, ErrorFieldValue> => {
  const view = new DataView(payload.buffer, payload.byteOffset, payload.byteLength)
  const values: Record<string, ErrorFieldValue> = {}
  for (const field of fields) {
    const size = SIZES[field.type]
    const count = field.arrayLength ?? 1
    if (field.offset + size * count > payload.byteLength) continue
    const read = Array.from({ length: count }, (_, index) => readScalar(view, field.offset + index * size, field.type))
    values[field.name] = field.arrayLength === undefined ? read[0] : read
  }
  return values
}

const hex = (value: number, digits: number): string => `0x${value.toString(16).toUpperCase().padStart(digits, '0')}`

const fieldLabels = (fields: readonly ErrorPayloadField[], values: Readonly<Record<string, ErrorFieldValue>>, names: ValueNames | undefined): Record<string, string> => {
  const labels: Record<string, string> = {}
  if (!names) return labels
  for (const field of fields) {
    const value = values[field.name]
    if (value === undefined || Array.isArray(value)) continue
    const label = names.name(field, Number(value), values)
    if (label !== undefined) labels[field.name] = label
  }
  return labels
}

const renderMessage = (tag: ErrorTagInfo | undefined, fields: Readonly<Record<string, ErrorFieldValue>>, labels: Readonly<Record<string, string>>, payload: Uint8Array, catalog: ErrorCatalog): string => {
  const template = tag?.message
  if (template && template.args.every((arg) => fields[arg.field] !== undefined && !Array.isArray(fields[arg.field]))) {
    // A field the message already names through its helper ("%s (0x%x)") keeps its other occurrences plain.
    const namedByHelper = new Set(template.args.filter((arg) => arg.via !== undefined).map((arg) => arg.field))
    const args: PrintfValue[] = template.args.map((arg) => {
      const value = fields[arg.field] as number | bigint
      const label = labels[arg.field]
      // The firmware passes this field through a helper: print what the helper prints, from its
      // published table or, failing that, the field's catalog name (esp_err_to_name → esp_errors).
      if (arg.via !== undefined) return catalog.valueName(arg.via, Number(value)) ?? label ?? value
      return label === undefined || namedByHelper.has(arg.field) ? value : { value, label }
    })
    return formatPrintf(template.format, args)
  }
  if (!tag) return payload.byteLength ? `payload ${[...payload].map((byte) => byte.toString(16).padStart(2, '0')).join(' ')}` : ''
  return Object.entries(fields)
    .filter(([name]) => name !== 'unused')
    .map(([name, value]) => `${name}=${Array.isArray(value) ? `[${value.join(', ')}]` : String(value)}${labels[name] === undefined ? '' : ` (${labels[name]})`}`)
    .join(', ')
}

/**
 * Decode one error frame: `[stream][u8 node_count][u8 depth][u32 schema_id]`
 * then per node `[u8 payload_length][u16 tag][u16 owner][payload]`
 * (errors.generated.json → packet). With `names`, annotated payload fields
 * (device IDs, enum values, error tags …) are named too.
 */
export const decodeErrorPacket = (frame: Uint8Array, catalog: ErrorCatalog, names?: ValueNames): ErrorReport => {
  const view = new DataView(frame.buffer, frame.byteOffset, frame.byteLength)
  if (frame.byteLength < 7) {
    return { nodeCount: 0, depth: 0, schemaId: 0, schemaMatches: false, truncated: false, corrupt: false, nodes: [], malformed: `frame of ${frame.byteLength} bytes is shorter than the 7-byte header` }
  }
  const nodeCount = view.getUint8(1)
  const depth = view.getUint8(2)
  const schemaId = view.getUint32(3, true)
  const nodes: ErrorNodeReport[] = []
  let offset = 7
  let malformed: string | undefined
  while (nodes.length < nodeCount) {
    if (offset + 5 > frame.byteLength) {
      malformed = `node ${nodes.length} header runs past the frame`
      break
    }
    const payloadLength = view.getUint8(offset)
    const tagId = view.getUint16(offset + 1, true)
    const ownerId = view.getUint16(offset + 3, true)
    offset += 5
    if (offset + payloadLength > frame.byteLength) {
      malformed = `node ${nodes.length} payload runs past the frame`
      break
    }
    const payload = frame.slice(offset, offset + payloadLength)
    offset += payloadLength
    const tag = catalog.tag(tagId)
    const fields = tag ? readFields(tag.fields, payload) : {}
    const labels = tag ? fieldLabels(tag.fields, fields, names) : {}
    nodes.push({
      tagId,
      ownerId,
      tag,
      owner: catalog.owner(ownerId),
      level: tag ? catalog.level(tag.level) : undefined,
      payload,
      fields,
      labels,
      payloadMismatch: tag !== undefined && payload.byteLength !== tag.payloadSize,
      message: renderMessage(tag, fields, labels, payload, catalog),
    })
  }
  if (!malformed && offset < frame.byteLength) malformed = `${frame.byteLength - offset} bytes after the last node`
  return {
    nodeCount,
    depth,
    schemaId,
    schemaMatches: schemaId === catalog.schemaId,
    truncated: depth !== catalog.depthCorrupt && depth > nodeCount,
    corrupt: depth === catalog.depthCorrupt,
    nodes,
    malformed,
  }
}

/** `ERR_DEV_NOT_FOUND` or `tag 0xA10B` when the catalog doesn't know it. */
export const errorTagName = (catalog: ErrorCatalog, id: number): string => catalog.tag(id)?.name ?? `tag ${hex(id, 4)}`
export const errorOwnerName = (catalog: ErrorCatalog, id: number): string => catalog.owner(id)?.name ?? `owner ${hex(id, 4)}`
