/** Explicit fixed-width C wire types and their convenient aliases. */
export type PacketScalarType =
  | 'u8' | 'uint8_t' | 'uint8'
  | 'i8' | 'int8_t' | 'int8'
  | 'u16' | 'uint16_t' | 'uint16'
  | 'i16' | 'int16_t' | 'int16'
  | 'u32' | 'uint32_t' | 'uint32'
  | 'i32' | 'int32_t' | 'int32'
  | 'u64' | 'uint64_t' | 'uint64'
  | 'i64' | 'int64_t' | 'int64'
  | 'f32' | 'float'
  | 'f64' | 'double'
  | 'bool' | '_Bool'
  | 'bitmask8' | 'bitmask16' | 'bitmask32' | 'bitmask64'

export type PacketScalarValue = number | bigint | boolean
export interface PacketStructValues {
  readonly [field: string]: PacketValue
}
export type PacketValue = PacketScalarValue | readonly PacketScalarValue[] | Uint8Array | string | PacketStructValues

export interface ScalarField {
  readonly kind: 'scalar'
  readonly name: string
  readonly type: PacketScalarType
  /** Exact element count for a C fixed-size scalar array. Omit for a scalar. */
  readonly length?: number
}

export interface BytesField {
  readonly kind: 'bytes'
  readonly name: string
  /** Exact byte count for a C fixed-size uint8_t array. Omit for variable bytes. */
  readonly length?: number
}

export interface TextField {
  readonly kind: 'text'
  readonly name: string
  /** Exact byte capacity for a fixed C char array. Omit for variable UTF-8 bytes. */
  readonly length?: number
  /** Append a NUL byte before fixed-capacity padding or variable output. */
  readonly nulTerminate?: boolean
}

export interface StructField {
  readonly kind: 'struct'
  readonly name: string
  readonly fields: readonly PacketField[]
}

export type PacketField = ScalarField | BytesField | TextField | StructField

export interface PacketSchema {
  readonly name?: string
  /** First wire byte, normally the generated RX/TX packet class. */
  readonly classHeader: number
  /** Second wire byte, normally HEADER_packet_<function>. */
  readonly functionHeader: number
  readonly fields: readonly PacketField[]
}
