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
  readonly default?: number
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
  readonly limits: { readonly connectors_max: number; readonly providers_per_connector_max: number; readonly name_max: number; readonly frame_max: number }
  readonly ble: {
    readonly service: string
    readonly service_symbol: string
    readonly is_primary: boolean
    readonly characteristics: readonly {
      readonly symbol: string
      readonly uuid: string
      readonly name: string
      readonly write: boolean
      readonly notify: boolean
      readonly indicate: boolean
      readonly tx_buffer_size: number
      readonly rx_buffer_size: number
    }[]
  }
  readonly streams: readonly {
    readonly name: string
    readonly header: string
    readonly connector: string
    readonly connector_id: number
    readonly alias: string
    readonly description: string
    readonly max_frame: number
    readonly ble?: { readonly notify?: string; readonly write?: string }
  }[]
  readonly bindings: readonly {
    readonly connector: string
    readonly connector_id: number
    readonly direction: string
    readonly provider: string
    readonly provider_id: number
    readonly endpoint: number
    readonly endpoint_symbol: string
    readonly when?: string
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
  /** SYS_I2C_BUS_*: onboard devices sit on `internal`, user devices go on `user`. */
  readonly i2c_buses: { readonly internal: number; readonly user: number }
  readonly devices: readonly {
    readonly id: number
    readonly symbol: string
    readonly name: string
    readonly driver?: string
    readonly descriptor?: string
    readonly title?: string
    /** The board installs it at boot (bring-up switch on). */
    readonly installed: boolean
    readonly i2c?: { readonly bus: number; readonly address: number }
    /** Pins it takes on other devices; `mode` is a sys_io_mode_e symbol. */
    readonly pins?: readonly { readonly use: string; readonly device: number; readonly pin: number; readonly mode: string }[]
  }[]
  /** Pins the board sets up itself at boot (status LEDs, supply switches …). */
  readonly pin_setup: readonly { readonly device: number; readonly pin: number; readonly mode: string; readonly level?: boolean; readonly installed: boolean; readonly label?: string }[]
  /** ESP pins the board uses outside sys_io (I2C buses …). */
  readonly reserved_pins: readonly { readonly device: number; readonly pin: number; readonly label: string }[]
}

/** `data-structures/vm/blocks/index.generated.json` (the parts the app reads) */
export interface GeneratedVmBlocksIndex {
  readonly blocks: readonly { readonly id: number; readonly name: string; readonly title: string }[]
}

export interface GeneratedVmBlockEditorMetadata {
  readonly id_kind?: 'device' | 'pin'
  readonly device_field?: string
  readonly contract?: string
  readonly hidden_by_default?: boolean
  readonly extended_view_show?: boolean
  readonly let_user_select_available?: string
  readonly dynamic_input?: number
}

export interface GeneratedVmBlockPin extends GeneratedVmBlockEditorMetadata {
  readonly index: number
  readonly name: string
  readonly title: string
  readonly value: string
  readonly description?: string
  readonly required?: boolean
}

export interface GeneratedVmBlockStateField extends GeneratedVmBlockEditorMetadata {
  readonly name: string
  readonly c_type: string
  readonly offset: number
  readonly size?: number
  readonly description?: string
  /** user: set by the app; derived: computed by the compiler; runtime / padding: 0 on the wire. */
  readonly source: string
  readonly enum_ref?: string
  readonly derived?: string
  readonly flexible?: boolean
  readonly element_size?: number
}

export interface GeneratedVmBlockOpcode {
  readonly symbol: string
  readonly value: number
  readonly alias: string
  readonly description?: string
  readonly pops: number
  readonly pushes: number
  readonly operand: string
}

/** `data-structures/vm/blocks/block_*.generated.json` */
export interface GeneratedVmBlockFile {
  readonly kind: string
  readonly id: number
  readonly name: string
  readonly title: string
  readonly category: string
  readonly description: string
  readonly activation: { readonly kind: string; readonly description?: string }
  readonly inputs: { readonly min: number; readonly max: number; readonly pins: readonly GeneratedVmBlockPin[] }
  readonly outputs: { readonly min: number; readonly max: number; readonly pins: readonly GeneratedVmBlockPin[] }
  readonly rules: readonly { readonly rule: string; readonly error: string }[]
  /** What the block's ENO is called, when "When done" is not right (`//@eno`). */
  readonly eno?: { readonly title: string; readonly description?: string }
  /** `//@view simple`: the face shows it all, no detailed view. */
  readonly view?: 'simple'
  readonly state?: { readonly size: number; readonly fields: readonly GeneratedVmBlockStateField[] }
  readonly min_custom_len: number
  readonly enums?: Readonly<Record<string, { readonly alias?: string | null; readonly members: readonly { readonly name: string; readonly value: number; readonly alias?: string | null }[] }>>
  readonly encoding?: {
    readonly kind: string
    readonly constant_type: string
    readonly stack_max: number
    readonly opcodes: readonly GeneratedVmBlockOpcode[]
    readonly examples?: readonly { readonly title: string; readonly constants: readonly number[]; readonly code: readonly string[]; readonly custom_data: string }[]
  }
}

/** `data-structures/vm/vm-program.generated.json` (the parts the app reads) */
export interface GeneratedVmProgramFile {
  readonly class_header: string
  readonly types: readonly { readonly symbol: string; readonly value: number; readonly alias: string; readonly memory_width: number; readonly wire_width: number; readonly wire_type: string }[]
  readonly constants: Readonly<Record<string, { readonly value: number }>>
  readonly limits: Readonly<Record<string, { readonly value: number }>>
  readonly sizes: Readonly<Record<string, number>>
  readonly arena: { readonly alignment: number }
  readonly packets: readonly {
    readonly packet_header: string
    readonly symbol: string
    readonly batch?: { readonly count_type: string; readonly max: number }
    readonly record: { readonly name: string; readonly size: number }
  }[]
  readonly unions: Readonly<Record<string, { readonly cases: readonly { readonly symbol: string; readonly value: number; readonly record: { readonly size: number } }[] }>>
  readonly telemetry: { readonly stream: string; readonly class_header: string; readonly frames: readonly { readonly packet_header: string; readonly symbol: string }[] }
}

export interface GeneratedVmModelField {
  readonly name: string
  readonly kind: string
  readonly c_type?: string
  readonly bit_width?: number
  readonly annotations?: Readonly<Record<string, unknown>>
  readonly fields?: readonly GeneratedVmModelField[]
}

/** `data-structures/vm/vm-model.generated.json` (the parts the app reads) */
export interface GeneratedVmModelFile {
  readonly enums: Readonly<Record<string, { readonly members: readonly { readonly name: string; readonly value: number }[] }>>
  readonly structures: readonly { readonly name: string; readonly annotations?: Readonly<Record<string, unknown>>; readonly fields: readonly GeneratedVmModelField[] }[]
}
