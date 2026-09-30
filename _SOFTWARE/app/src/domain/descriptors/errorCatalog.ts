import { DescriptorError } from './commandCatalog'
import type { GeneratedErrorsFile } from './generatedTypes'

export type ErrorPayloadType = 'uint8_t' | 'int8_t' | 'uint16_t' | 'int16_t' | 'uint32_t' | 'int32_t' | 'uint64_t' | 'int64_t' | 'float' | 'double'

export interface ErrorPayloadField {
  readonly name: string
  readonly type: ErrorPayloadType
  /** Byte offset in the native C struct. */
  readonly offset: number
  readonly arrayLength?: number
  /** The value is a member of this enums.json enum. */
  readonly enumRef?: string
  /**
   * The value is an ID named in another catalog. `parentField` is the payload field it depends on:
   * the class byte of an `rx-packet`, the contract type of a `contract-feature`.
   */
  readonly idRef?: { readonly kind: string; readonly parentField?: string }
}

export interface ErrorMessageTemplate {
  /** C printf format, as in the firmware's LOG_BODY macro. */
  readonly format: string
  readonly args: readonly { readonly field: string; readonly via?: string }[]
}

export interface ErrorTagInfo {
  readonly name: string
  readonly id: number
  /** Default severity (`se_level_e` value). */
  readonly level: number
  readonly sourceFile: string
  readonly payloadSize: number
  readonly fields: readonly ErrorPayloadField[]
  readonly message?: ErrorMessageTemplate
  readonly messageUnavailable?: string
}

export interface ErrorOwnerInfo {
  readonly name: string
  readonly id: number
  readonly sourceFile: string
}

export interface ErrorLevelInfo {
  readonly name: string
  readonly value: number
  readonly alias: string
}

export interface ErrorCatalog {
  /** SE_schema_id() of the maps this catalog was generated from. */
  readonly schemaId: number
  /** Stream byte of error frames. */
  readonly stream: number
  /** `depth` value meaning a corrupt or over-depth chain. */
  readonly depthCorrupt: number
  readonly levels: readonly ErrorLevelInfo[]
  tag(id: number): ErrorTagInfo | undefined
  tagByName(name: string): ErrorTagInfo | undefined
  owner(id: number): ErrorOwnerInfo | undefined
  level(value: number): ErrorLevelInfo | undefined
  /** Text the firmware's `via` helper prints for a value, when the helper is a published table. */
  valueName(via: string, value: number): string | undefined
  /** esp_err_t name (`ESP_ERR_NO_MEM`) of the ESP-IDF the firmware is built with. */
  espError(code: number): string | undefined
  /** Name of a device contract's feature (`set_level`), by contract type value and feature ID. */
  contractFeature(contract: number, feature: number): string | undefined
}

const PAYLOAD_TYPES: ReadonlySet<string> = new Set<ErrorPayloadType>(['uint8_t', 'int8_t', 'uint16_t', 'int16_t', 'uint32_t', 'int32_t', 'uint64_t', 'int64_t', 'float', 'double'])

// The error packet decoder (domain/decoder/errorPacket.ts) reads exactly this layout.
const EXPECTED_HEADER = 'node_count:uint8_t,depth:uint8_t,schema_id:uint32_t'
const EXPECTED_NODE = 'payload_length:uint8_t,tag:uint16_t,owner:uint16_t'
const layoutKey = (fields: readonly { readonly name: string; readonly type: string }[]): string => fields.map((field) => `${field.name}:${field.type}`).join(',')

export const buildErrorCatalog = (file: GeneratedErrorsFile): ErrorCatalog => {
  if (file.packet.byte_order !== 'little') throw new DescriptorError(`Unsupported error packet byte order '${file.packet.byte_order}'.`)
  if (layoutKey(file.packet.header) !== EXPECTED_HEADER) throw new DescriptorError(`Error packet header changed to ${layoutKey(file.packet.header)}; update the decoder.`)
  if (layoutKey(file.packet.node) !== EXPECTED_NODE) throw new DescriptorError(`Error packet node changed to ${layoutKey(file.packet.node)}; update the decoder.`)
  const stream = Number.parseInt(file.packet.stream, 16)
  if (!/^0x[0-9a-f]{2}$/i.test(file.packet.stream)) throw new DescriptorError(`Error stream '${file.packet.stream}' is not one hex byte.`)

  const tags = new Map<number, ErrorTagInfo>()
  const tagNames = new Map<string, ErrorTagInfo>()
  for (const tag of file.tags) {
    if (tags.has(tag.id)) throw new DescriptorError(`Error tag ID 0x${tag.id.toString(16)} is used twice.`)
    const fields = tag.payload.fields.map((field) => {
      if (!PAYLOAD_TYPES.has(field.type)) throw new DescriptorError(`${tag.name}.${field.name}: unsupported payload type '${field.type}'.`)
      return {
        name: field.name,
        type: field.type as ErrorPayloadType,
        offset: field.offset,
        arrayLength: field.array_len,
        enumRef: field.enum_ref,
        idRef: field.id && { kind: field.id.kind, parentField: field.id.parent_field },
      }
    })
    const info: ErrorTagInfo = {
      name: tag.name,
      id: tag.id,
      level: tag.level,
      sourceFile: tag.source_file,
      payloadSize: tag.payload.size,
      fields,
      message: tag.message ?? undefined,
      messageUnavailable: tag.message_unavailable,
    }
    tags.set(tag.id, info)
    tagNames.set(tag.name, info)
  }
  const owners = new Map(file.owners.map((owner) => [owner.id, { name: owner.name, id: owner.id, sourceFile: owner.source_file }]))
  const levels = new Map(file.levels.map((level) => [level.value, level]))

  return {
    schemaId: file.schema_id,
    stream,
    depthCorrupt: file.packet.depth_corrupt,
    levels: file.levels,
    tag: (id) => tags.get(id),
    tagByName: (name) => tagNames.get(name),
    owner: (id) => owners.get(id),
    level: (value) => levels.get(value),
    valueName: (via, value) => {
      const table = file.value_names[via]
      return table ? table.values[String(value)] ?? table.default : undefined
    },
    espError: (code) => file.esp_errors.codes[String(code)]?.name,
    contractFeature: (contract, feature) => file.contract_features.contracts[String(contract)]?.features[feature],
  }
}
