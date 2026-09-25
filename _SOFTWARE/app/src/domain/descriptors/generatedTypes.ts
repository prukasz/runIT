/**
 * The parts of the generated descriptor files the app reads
 * (`data-structures/contracts`, `data-structures/settings`). The JSON files
 * are imported typed as these, so a generator change that breaks them fails
 * the type check. Schemas: `data-structures/schema/*.schema.json`.
 */

export interface GeneratedChoice {
  readonly symbol: string
  readonly enum?: string
  readonly value: number
  readonly alias?: string
  readonly description?: string
}

export interface GeneratedField {
  /** C type: uint8_t, int32_t, float, char (flexible_array) … */
  readonly type: string
  readonly alias?: string
  /**
   * false means the value may be 0 / the sentinel, not that it may be left
   * off the wire: decoders need the whole packed struct (F-GAP-10).
   */
  readonly required?: boolean
  readonly sentinel?: number | null
  readonly note?: string
  readonly unit?: string
  readonly min?: number
  readonly max?: number
  readonly array_len?: number
  readonly one_of?: readonly GeneratedChoice[]
  readonly enum_ref?: string
  /** A trailing `char name[]`: UTF-8 text closed by NUL. */
  readonly flexible_array?: boolean
  readonly encoding?: string
  readonly terminator?: string
}

export interface GeneratedLayout {
  readonly field_order: readonly string[]
  /** `undefined` only because TypeScript types a JSON import that way for keys other packets have. */
  readonly fields: Readonly<Record<string, GeneratedField | undefined>>
}

export interface GeneratedPacket extends GeneratedLayout {
  readonly id: string
  readonly packet: string
  readonly packet_header: string
  readonly response?: GeneratedLayout & { readonly struct: string }
}

export interface GeneratedContractsFile {
  readonly response_stream: {
    readonly class_header: string
    readonly matching: string
    /** Enum (enums.json) of the response status byte. */
    readonly status_enum: string
  }
  readonly catalogs: readonly {
    readonly id: string
    readonly title: string
    readonly description: string
    readonly class_header: string
    readonly contracts: readonly GeneratedPacket[]
  }[]
}

export interface GeneratedSettingsFile {
  readonly settings: readonly {
    readonly tag: string
    readonly title: string
    readonly description: string
    readonly class_header: string
    readonly packets: readonly GeneratedPacket[]
  }[]
}

/** `data-structures/enums.json` */
export interface GeneratedEnumsFile {
  readonly enums: Readonly<Record<string, {
    readonly alias?: string
    readonly members: readonly { readonly name: string; readonly value: number; readonly alias?: string | null; readonly description?: string | null }[]
  } | undefined>>
}

/** `data-structures/streams/streams.generated.json` */
export interface GeneratedStreamsFile {
  readonly ble: { readonly service: string }
  readonly streams: readonly {
    readonly name: string
    readonly header: string
    readonly connector: string
    readonly connector_id: number
    readonly alias: string
    readonly description: string
    readonly ble?: { readonly notify?: string; readonly write?: string }
  }[]
}

export interface GeneratedErrorPayloadField {
  readonly name: string
  readonly type: string
  readonly offset: number
  readonly array_len?: number
  /** The value is a member of this enums.json enum. */
  readonly enum_ref?: string
  /** The value is an ID named in another catalog (errors.generated.json id_kinds). */
  readonly id?: { readonly kind: string; readonly parent_field?: string }
}

export interface GeneratedErrorTag {
  readonly name: string
  readonly id: number
  readonly level: number
  readonly source_file: string
  readonly payload: { readonly size: number; readonly fields: readonly GeneratedErrorPayloadField[] }
  readonly message: { readonly format: string; readonly args: readonly { readonly field: string; readonly via?: string }[] } | null
  readonly message_unavailable?: string
}

/** `data-structures/errors/errors.generated.json` */
export interface GeneratedErrorsFile {
  readonly schema_id: number
  readonly packet: {
    readonly stream: string
    readonly byte_order: string
    readonly header: readonly { readonly name: string; readonly type: string }[]
    readonly node: readonly { readonly name: string; readonly type: string }[]
    readonly depth_corrupt: number
  }
  readonly levels: readonly { readonly name: string; readonly value: number; readonly alias: string }[]
  readonly owners: readonly { readonly name: string; readonly id: number; readonly source_file: string }[]
  readonly tags: readonly GeneratedErrorTag[]
  /** `undefined` only because TypeScript types a JSON import that way for keys other entries have. */
  readonly value_names: Readonly<Record<string, { readonly values: Readonly<Record<string, string | undefined>>; readonly default?: string } | undefined>>
  /** Every `@id` kind the payload fields use, with where its names live. */
  readonly id_kinds: Readonly<Record<string, string>>
  /** Feature names per device contract (key: contract type value; feature ID = index). */
  readonly contract_features: { readonly contracts: Readonly<Record<string, { readonly features: readonly string[] } | undefined>> }
  readonly esp_errors: { readonly codes: Readonly<Record<string, { readonly name: string; readonly description: string } | undefined>> }
}

/** `data-structures/board/board.generated.json` */
export interface GeneratedBoardFile {
  readonly devices: readonly { readonly id: number; readonly symbol: string; readonly name: string; readonly driver?: string; readonly descriptor?: string; readonly title?: string }[]
}

/** `data-structures/vm/blocks/index.generated.json` (the parts the app reads) */
export interface GeneratedVmBlocksIndex {
  readonly blocks: readonly { readonly id: number; readonly name: string; readonly title: string }[]
}
