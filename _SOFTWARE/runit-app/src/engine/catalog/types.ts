/*
 * The block catalog in the engine's own terms. generated/blocks/*.json (schema templates/vm-block.schema.json, version 3) is
 * read once by fromGenerated.ts and turned into these types; the rest of the engine, and a UI built on it, never sees
 * the file format. A block's contract and device commands are resolved in here, so a variant carries its final fields.
 */

export type Category = 'flow' | 'logic' | 'data' | 'time' | 'device' | 'system'
export type Integration = 'core' | 'contract' | 'custom'
export type ActivationKind = 'enabled' | 'enable-rising'

/** What a wire carries. `flow` is an execution wire (enable / gate); `object` a whole object of the program; `cell` an element of a pointer object. */
export type ValueKind = 'bool' | 'int' | 'float' | 'any' | 'object' | 'cell' | 'flow'
/** What a field holds. */
export type FieldType = 'enum' | 'number' | 'boolean' | 'string' | 'pin' | 'device' | 'accessor' | 'action' | 'event'
/** The type of a field in the block's state bytes (or in a command's argument struct). */
export type RawType = 'u8' | 'i8' | 'u16' | 'i16' | 'u32' | 'i32' | 'float' | 'bytes'
export type InputMode = 'none' | 'optional' | 'required'

export type FieldScalar = number | string | boolean

/** A hint for the user next to a value; `when` is evaluated by the UI on what it knows statically. Kept as the descriptor wrote it. */
export interface Hint {
  readonly text?: string
  readonly min?: number
  readonly max?: number
  readonly unit?: string
  readonly when?: unknown
}

export interface FieldOption {
  readonly value: FieldScalar
  readonly alias: string
  readonly description?: string
}

/** A value the block keeps. With `input` it is also an input pin: a wired value wins over the stored one. */
export interface Field {
  readonly id: string
  readonly alias: string
  readonly description?: string
  readonly type: FieldType
  /** For an enum: the generated enum it came from (aliases come from there). */
  readonly enum?: string
  readonly options?: readonly FieldOption[]
  readonly default?: FieldScalar
  readonly min?: number
  readonly max?: number
  readonly step?: number
  readonly unit?: string
  /** Where and how the encoder writes the value: in the block's state (core and custom blocks). */
  readonly rawType?: RawType
  readonly size?: number
  readonly offset?: number
  /** The wire value is the shown value times `scale`. */
  readonly scale?: number
  /** The wire value that means "any" (a filter). */
  readonly any?: number
  /** For an event or pin field: the id of the device field of this block that says whose events / pins. */
  readonly deviceField?: string
  readonly required: boolean
  /** Hardcoded by the variant: not shown and, with `input` none, not changeable. */
  readonly fixed?: FieldScalar
  readonly input: InputMode
  readonly accepts: readonly FieldType[]
  readonly hidden: boolean
  readonly hints: readonly Hint[]
}

export interface Pin {
  readonly kind: 'pin'
  readonly id: string
  readonly alias: string
  readonly description?: string
  readonly value: ValueKind
  readonly required: boolean
  /** An output that only one of its siblings fires (if: yes / no, switch: each case). */
  readonly flow?: 'conditional'
  /** An input: the field whose value stands in while the pin is unwired. */
  readonly overrides?: string
  /** An input the block writes. */
  readonly writes: boolean
  readonly hints: readonly Hint[]
  /** Set when the pin comes from a field with `input` (the field's id equals the pin's). */
  readonly field?: string
}

/** A run of pins the user adds and removes (the inputs of an expression, the cases of a switch). */
export interface PinGroup {
  readonly kind: 'group'
  readonly alias: string
  readonly description?: string
  readonly value: ValueKind
  readonly min: number
  readonly max: number
  readonly label: 'shown' | 'hidden'
  readonly required: boolean
  readonly flow?: 'conditional'
}

export type PinSpec = Pin | PinGroup

export interface Activation {
  readonly kind: ActivationKind
  readonly text: string
}

/** What a variant of a device block runs: a command of its contract or of its device type. */
export interface Operation {
  readonly source: 'contract' | 'device'
  /** The contract or device type id. */
  readonly of: string
  /** The command's id (`LEVEL`, `fault`). */
  readonly command: string
  /** The side of the command the block runs (`cmd_dir_e`): a read fills the outputs, a write drives the device. */
  readonly dir: number
  /**
   * What the block's state holds in `op`: the u16 `SYS_DEVICE_OP(contract, n)` = `(contract_id << 8) | n` of a contract command, or
   * `SYS_DEVICE_OWN_OP(n)` = `0xFF00 | n` of a device's own command (sys_device.h). Not the command's bare `op` of the descriptor.
   */
  readonly value: number
}

export interface Variant {
  readonly id: string
  readonly alias: string
  readonly description?: string
  readonly header?: { readonly subtitle?: string }
  readonly body?: readonly string[]
  readonly activation?: Activation
  readonly operation?: Operation
  /** The variant's own fields: for a device block, the command's arguments (in the command's order), each with its input pin. */
  readonly fields: readonly Field[]
  readonly inputs: readonly PinSpec[]
  /** For a device block: one output per value the command returns. */
  readonly outputs: readonly PinSpec[]
  /** Why the variant cannot be compiled yet (a descriptor gap), when it cannot. */
  readonly unsupported?: string
}

export interface Signal {
  readonly alias: string
  readonly description: string
  readonly required?: boolean
}

export interface BlockType {
  /** The descriptor's id (`if`, `io`). */
  readonly id: string
  /** `block_type` of the ADD_BLOCK record (`vm_block_ids.h`). */
  readonly wireId: number
  readonly alias: string
  readonly description: string
  readonly category: Category
  readonly order: number
  readonly tags: readonly string[]
  readonly integration: Integration
  readonly activation: Activation
  readonly header: { readonly subtitle?: string }
  readonly body: readonly string[]
  /** For a core block: the editor that owns its formula or cases (`expression`, `switch-cases`). The engine's codec of that name encodes it. */
  readonly editor?: string
  /** For a formula editor: the generated enum that holds the opcodes of the block's stack machine, and the type of its constants (`opcodes` of the descriptor). */
  readonly opcodes?: { readonly enum: string; readonly constant: 'f32' | 'u32' }
  /** The block's enable, when it has a say in it (edge: Signal, latch: Set). */
  readonly enable?: Signal
  /** What the block's enable-out means, when more than "ran". */
  readonly eno?: Signal
  /** A block that owns a run of the blocks below it (for). `offset`: where the run's [start, end) goes in the state. */
  readonly span?: { readonly offset: number }
  readonly device?: { readonly contract?: string; readonly deviceType?: string }
  readonly inputs: readonly PinSpec[]
  readonly outputs: readonly PinSpec[]
  readonly fields: readonly Field[]
  readonly variants: readonly Variant[]
  /** The `.state_len` of the block's type (descriptor `state_len`): the bytes the board needs in the state at least, and refuses a block below. */
  readonly stateLength: number
  /** The state is longer than `stateLength` and its size depends on what the user wrote (formula, cases): the codec of the block's editor builds it. */
  readonly stateVariable: boolean
  readonly guide?: string
}
