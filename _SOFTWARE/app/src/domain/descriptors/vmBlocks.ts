import { DescriptorError } from './commandCatalog'
import type { GeneratedVmBlockEditorMetadata, GeneratedVmBlockFile, GeneratedVmBlockOpcode, GeneratedVmTemplatePart } from './generatedTypes'

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
  readonly letUserSelectAvailable?: string
  readonly dynamicInput?: number
}

/** A line of the block's face: literal text, or a reference resolved against the block (an input pin's wired source or its constant, an output pin, a setting, the block title). */
export type VmTemplatePart = GeneratedVmTemplatePart
export type VmTemplate = readonly VmTemplatePart[]

/** The face's line under the title: the block's words, then the value it is set to, which the face draws apart. */
export interface VmBlockHeader {
  readonly lead: VmTemplate
  readonly value?: VmTemplate
}

export interface VmBlockPin extends VmBlockEditorMetadata {
  /** The setting used as a constant while this pin is unwired: the pin is hidden until wired, and the user enters the constant instead. */
  readonly overrides?: string
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
  /** Index of the input pin that replaces this setting while it is wired. */
  readonly overriddenBy?: number
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

/** What a block's ENO is called and does: the header's `//@eno`, else this. */
export interface VmBlockEno {
  readonly title: string
  readonly description: string
}

export const DEFAULT_ENO: VmBlockEno = { title: 'When done', description: 'true while the block acted; drag it onto the Run when of another block to run that block only then' }

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
  readonly eno: VmBlockEno
  /** The block has a detailed view: it has inputs with a constant to show, or a formula. Other blocks are their face. */
  readonly hasDetail: boolean
  /** The line under the title on the face (`//@header`); none: the title alone. */
  readonly header?: VmBlockHeader
  /** Private state: its fixed size and fields (empty for stateless blocks). */
  readonly stateSize: number
  readonly fields: readonly VmBlockField[]
  readonly minCustomLen: number
  /** Enum members a field may take (a `*_CNT` count member excluded), by enum name; `alias` is how a face words it (`<`). */
  readonly enums: ReadonlyMap<string, readonly { readonly name: string; readonly value: number; readonly alias?: string }[]>
  readonly encoding?: VmBlockEncoding
}

/** Members of an enum without their shared prefix (`VM_TIMER_UNIT_MS` → `MS`): how the editor names and stores them. */
export const enumMemberLabels = (members: readonly { readonly name: string; readonly value: number }[]): { label: string; value: number }[] => {
  const names = members.map((member) => member.name)
  let prefix = names[0] ?? ''
  for (const name of names) while (!name.startsWith(prefix)) prefix = prefix.slice(0, -1)
  prefix = prefix.slice(0, prefix.lastIndexOf('_') + 1)
  return members.map((member) => ({ label: member.name.slice(prefix.length) || member.name, value: member.value }))
}

/** How the app words an enum member: `SET_DOMINANT` → `Set dominant`. One-word members (`TON`, `MS`, `RISING`) are already short and stay. Stored values keep the label. */
export const enumAlias = (label: string): string => {
  if (!label.includes('_')) return label
  const words = label.toLowerCase().split('_').join(' ')
  return words.charAt(0).toUpperCase() + words.slice(1)
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
    hiddenByDefault: entry.hidden_by_default, letUserSelectAvailable: entry.let_user_select_available, dynamicInput: entry.dynamic_input,
  })
  const pins = (side: GeneratedVmBlockFile['inputs']): VmBlockPins => ({
    min: side.min,
    max: side.max,
    pins: side.pins.map((pin) => ({ index: pin.index, name: pin.name, title: pin.title, value: pin.value, description: pin.description, overrides: pin.overrides, required: pin.required ?? false, ...metadata(pin) })),
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
      overriddenBy: field.overridden_by,
      flexible: field.flexible ?? false,
      ...metadata(field),
    }
  })
  const enums = new Map(Object.entries(file.enums ?? {}).map(([name, entry]) => [name, entry.members.filter((member) => !member.name.endsWith('_CNT')).map(({ name: member, value, alias }) => ({ name: member, value, ...(alias ? { alias } : {}) }))]))
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
    eno: { title: file.eno?.title ?? DEFAULT_ENO.title, description: file.eno?.description ?? DEFAULT_ENO.description },
    hasDetail: !!encoding || pins(file.inputs).pins.some((pin) => pin.overrides),
    ...(file.header ? { header: file.header } : {}),
    stateSize: file.state?.size ?? 0,
    fields,
    minCustomLen: file.min_custom_len,
    enums,
    ...(encoding ? { encoding } : {}),
  }
}
