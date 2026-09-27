import { DescriptorError } from './commandCatalog'
import type { GeneratedVmBlockEditorMetadata, GeneratedVmBlockFile, GeneratedVmBlockOpcode } from './generatedTypes'

/*
 * The VM block palette (data-structures/vm/blocks/block_*.generated.json):
 * pins, private state layout, enums, and the bytecode of the expression
 * blocks. The block compiler builds 0x45 records from it.
 */

export interface VmBlockEditorMetadata {
  readonly idKind?: 'device' | 'pin'
  readonly deviceField?: string
  readonly contract?: string
  readonly hiddenByDefault?: boolean
  readonly extendedViewShow?: boolean
  readonly letUserSelectAvailable?: string
  readonly dynamicInput?: number
}

export interface VmBlockPin extends VmBlockEditorMetadata {
  readonly index: number
  readonly name: string
  readonly title: string
  /** Value kind (vm-blocks index `value_kinds`): bool, u8, u32, i32, f32, scalar, gate, object, ptr-cell. */
  readonly value: string
  readonly description?: string
  readonly required: boolean
}

export interface VmBlockPins {
  readonly min: number
  readonly max: number
  /** Listed pins; past the last one, the last repeats (EXPR's inputs). */
  readonly pins: readonly VmBlockPin[]
}

export interface VmBlockField extends VmBlockEditorMetadata {
  readonly name: string
  readonly cType: string
  readonly offset: number
  readonly size: number
  /** user: from the block's settings; derived: computed by the compiler; runtime / padding: 0 on the wire. */
  readonly source: 'user' | 'derived' | 'runtime' | 'padding'
  readonly enumRef?: string
  readonly description?: string
  /** A trailing array (the expression blocks' constants and code). */
  readonly flexible: boolean
}

export interface VmOpcode {
  readonly symbol: string
  readonly value: number
  readonly alias: string
  readonly description?: string
  readonly pops: number
  readonly pushes: number
  /** none, input (an input pin index) or const (a constant index): one byte after the opcode. */
  readonly operand: string
}

export interface VmBlockEncoding {
  /** Type of the 4-byte constants: f32 (EXPR) or u32 (EXPR_BIT). */
  readonly constantType: string
  readonly stackMax: number
  readonly opcodes: readonly VmOpcode[]
  /** By alias (`+`, `in`), symbol (`VM_EXPR_ADD`) or symbol without its prefix (`ADD`). */
  readonly opcode: (token: string) => VmOpcode | undefined
  readonly examples: readonly { readonly title: string; readonly constants: readonly number[]; readonly code: readonly string[]; readonly customData: string }[]
}

export interface VmBlockType {
  readonly id: number
  /** The symbol without `VM_BLK_`: `EXPR`, `PERIODIC` … — the name project files use. */
  readonly key: string
  readonly symbol: string
  readonly title: string
  readonly category: string
  readonly description: string
  readonly activation: string
  readonly inputs: VmBlockPins
  readonly outputs: VmBlockPins
  readonly rules: readonly { readonly rule: string; readonly error: string }[]
  /** Private state: its fixed size and fields (empty for stateless blocks). */
  readonly stateSize: number
  readonly fields: readonly VmBlockField[]
  readonly minCustomLen: number
  /** Enum members a field may take (a `*_CNT` count member excluded), by enum name. */
  readonly enums: ReadonlyMap<string, readonly { readonly name: string; readonly value: number }[]>
  readonly encoding?: VmBlockEncoding
}

/** The pin descriptor for position `index`: a listed pin, or the last listed one repeated. */
export const blockPinAt = (pins: VmBlockPins, index: number): VmBlockPin | undefined => pins.pins[index] ?? pins.pins.at(-1)

const SOURCES = new Set(['user', 'derived', 'runtime', 'padding'])

const buildOpcodes = (opcodes: readonly GeneratedVmBlockOpcode[]) => {
  const list = opcodes.map((entry): VmOpcode => ({ ...entry }))
  const byToken = new Map<string, VmOpcode>()
  for (const entry of list) {
    byToken.set(entry.alias, entry)
    byToken.set(entry.symbol, entry)
    byToken.set(entry.symbol.replace(/^VM_[A-Z]+_/, ''), entry)
  }
  return { list, opcode: (token: string) => byToken.get(token) ?? byToken.get(token.toUpperCase()) }
}

export const buildVmBlockType = (file: GeneratedVmBlockFile): VmBlockType => {
  const metadata = (entry: GeneratedVmBlockEditorMetadata): VmBlockEditorMetadata => ({
    idKind: entry.id_kind, deviceField: entry.device_field, contract: entry.contract,
    hiddenByDefault: entry.hidden_by_default, extendedViewShow: entry.extended_view_show, letUserSelectAvailable: entry.let_user_select_available, dynamicInput: entry.dynamic_input,
  })
  const pins = (side: GeneratedVmBlockFile['inputs']): VmBlockPins => ({
    min: side.min,
    max: side.max,
    pins: side.pins.map((pin) => ({ index: pin.index, name: pin.name, title: pin.title, value: pin.value, description: pin.description, required: pin.required ?? false, ...metadata(pin) })),
  })
  const fields = (file.state?.fields ?? []).map((field): VmBlockField => {
    if (!SOURCES.has(field.source)) throw new DescriptorError(`${file.name}.${field.name}: unknown state source '${field.source}'.`)
    return {
      name: field.name,
      cType: field.c_type,
      offset: field.offset,
      size: field.size ?? field.element_size ?? 0,
      source: field.source as VmBlockField['source'],
      enumRef: field.enum_ref,
      description: field.description,
      flexible: field.flexible ?? false,
      ...metadata(field),
    }
  })
  const enums = new Map(Object.entries(file.enums ?? {}).map(([name, entry]) => [name, entry.members.filter((member) => !member.name.endsWith('_CNT')).map(({ name: member, value }) => ({ name: member, value }))]))
  for (const field of fields) if (field.enumRef && !enums.has(field.enumRef)) throw new DescriptorError(`${file.name}.${field.name} names enum ${field.enumRef}, which the block file lacks.`)
  const encoding = file.encoding && (() => {
    const { list, opcode } = buildOpcodes(file.encoding.opcodes)
    return {
      constantType: file.encoding.constant_type,
      stackMax: file.encoding.stack_max,
      opcodes: list,
      opcode,
      examples: (file.encoding.examples ?? []).map((example) => ({ title: example.title, constants: example.constants, code: example.code, customData: example.custom_data })),
    }
  })()
  return {
    id: file.id,
    key: file.name.replace(/^VM_BLK_/, ''),
    symbol: file.name,
    title: file.title,
    category: file.category,
    description: file.description,
    activation: file.activation.kind,
    inputs: pins(file.inputs),
    outputs: pins(file.outputs),
    rules: file.rules,
    stateSize: file.state?.size ?? 0,
    fields,
    minCustomLen: file.min_custom_len,
    enums,
    ...(encoding ? { encoding } : {}),
  }
}
