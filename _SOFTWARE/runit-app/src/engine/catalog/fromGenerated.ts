import type { EnumTables } from '../generated/wire'
import type {
  Activation,
  BlockType,
  Category,
  Field,
  FieldOption,
  FieldType,
  Hint,
  InputMode,
  Integration,
  Operation,
  Pin,
  PinSpec,
  RawType,
  FieldScalar,
  Signal,
  ValueKind,
  Variant,
} from './types'

/*
 * generated/blocks/block_*.json (schema version 3) -> BlockType. The one file that knows the descriptor format. A variant of
 * a device block names a command (`op`) of the block's contract (generated/contracts) or device type (generated/devices);
 * its arguments become the variant's fields and its returned values the variant's outputs, so everything downstream reads
 * one shape whatever the integration.
 */

/* ---- the descriptor shapes this adapter reads (loose: validated where used) ---- */

type Json = Record<string, unknown>

export interface GeneratedBlock extends Json {
  readonly id: string
  readonly wire_id: number
}

export interface GeneratedCommand extends Json {
  readonly id: string
  readonly op?: number
}

export interface GeneratedContract extends Json {
  readonly id: string
  /** The contract's number in `SYS_DEVICE_OP`: the high byte of an op code. */
  readonly contract_id?: number
  readonly commands?: readonly GeneratedCommand[]
}

export interface GeneratedDevice extends Json {
  readonly id: string
  readonly commands?: readonly GeneratedCommand[]
}

export interface GeneratedInput {
  readonly blocks: readonly GeneratedBlock[]
  readonly contracts: readonly GeneratedContract[]
  readonly devices: readonly GeneratedDevice[]
  readonly enums: EnumTables
}

export interface CatalogResult {
  readonly blocks: readonly BlockType[]
  /** Things the adapter could not take from the descriptors and had to assume: one line each, naming the block. */
  readonly warnings: readonly string[]
}

const str = (v: unknown): string | undefined => (typeof v === 'string' ? v : undefined)
const num = (v: unknown): number | undefined => (typeof v === 'number' ? v : undefined)
const bool = (v: unknown): boolean | undefined => (typeof v === 'boolean' ? v : undefined)
const obj = (v: unknown): Json | undefined => (v !== null && typeof v === 'object' && !Array.isArray(v) ? (v as Json) : undefined)
const list = (v: unknown): readonly unknown[] => (Array.isArray(v) ? v : [])
const scalar = (v: unknown): FieldScalar | undefined => (typeof v === 'number' || typeof v === 'string' || typeof v === 'boolean' ? v : undefined)

/** Only keys with a value: the types are `readonly x?: T`, and an `undefined` is not the same as absent. */
const opcodes = (v: unknown): BlockType['opcodes'] => {
  const o = obj(v)
  const name = str(o?.enum)
  return name && (o?.constant === 'f32' || o?.constant === 'u32') ? { enum: name, constant: o.constant } : undefined
}

const defined = <T extends object>(value: T): T => Object.fromEntries(Object.entries(value).filter(([, v]) => v !== undefined)) as T

const RAW_TYPES: readonly string[] = ['u8', 'i8', 'u16', 'i16', 'u32', 'i32', 'float', 'bytes']
const rawType = (v: unknown): RawType | undefined => {
  const text = str(v)
  if (text === undefined) return undefined
  return (RAW_TYPES.includes(text) ? text : 'bytes') as RawType
}

const FIELD_TYPES: readonly string[] = ['enum', 'number', 'boolean', 'string', 'pin', 'device', 'accessor', 'action', 'event']

/** A field type from a block descriptor's vocabulary or a contract argument's (`choice`, `integer`, `bitmask`). */
const fieldType = (v: unknown): FieldType => {
  const text = str(v) ?? 'number'
  if (text === 'choice') return 'enum'
  if (text === 'integer' || text === 'bitmask') return 'number'
  return (FIELD_TYPES.includes(text) ? text : 'string') as FieldType
}

const hints = (v: unknown): readonly Hint[] =>
  list(v).flatMap((h) => {
    const o = obj(h)
    return o ? [defined({ text: str(o.text), min: num(o.min), max: num(o.max), unit: str(o.unit), when: o.when })] : []
  })

const inputMode = (v: unknown): InputMode | undefined => (v === 'none' || v === 'optional' || v === 'required' ? v : undefined)

/** The choices of an enum field: the descriptor's own `options`, or the members of the generated enum it names. */
const options = (raw: Json, enums: EnumTables): readonly FieldOption[] | undefined => {
  const own = list(raw.options).flatMap((o): FieldOption[] => {
    const entry = obj(o)
    const value = scalar(entry?.value)
    const alias = str(entry?.alias) ?? str(entry?.label)
    return entry && value !== undefined && alias !== undefined ? [defined({ value, alias, description: str(entry.description) })] : []
  })
  if (own.length > 0) return own
  const named = str(raw.enum)
  const table = named ? enums[named] : undefined
  if (!table) return undefined
  return table.members.map((m) => defined({ value: m.value, alias: m.alias ?? m.name, description: m.description }))
}

const accepts = (v: unknown): readonly FieldType[] => list(v).map(fieldType)

/** A field from a block descriptor, or from a contract command's argument (`label` for `alias`, `optional` for the input mode). */
const field = (raw: Json, enums: EnumTables, override?: Json): Field => {
  const id = str(raw.id) ?? ''
  const mode = inputMode(override?.input) ?? inputMode(raw.input) ?? 'none'
  const fixed = scalar(override?.fixed) ?? scalar(raw.fixed)
  return defined({
    id,
    alias: str(override?.alias) ?? str(raw.alias) ?? str(raw.label) ?? id,
    description: str(raw.description),
    type: fieldType(raw.type),
    enum: str(raw.enum),
    options: options(raw, enums),
    default: scalar(raw.default),
    min: num(raw.min),
    max: num(raw.max),
    step: num(raw.step),
    unit: str(override?.unit) ?? str(raw.unit),
    rawType: rawType(raw.raw_type),
    size: num(raw.size),
    offset: num(raw.offset),
    scale: num(override?.scale) ?? num(raw.scale),
    any: num(raw.any),
    deviceField: str(raw.device_field),
    required: bool(override?.required) ?? bool(raw.required) ?? false,
    fixed,
    input: mode,
    accepts: override?.accepts !== undefined ? accepts(override.accepts) : accepts(raw.accepts),
    hidden: bool(override?.hidden) ?? bool(raw.hidden) ?? false,
    hints: hints(override?.hints ?? raw.hints),
  }) as Field
}

/** The kind of wire a field's input pin carries. */
const valueKindOf = (f: Field): ValueKind => {
  if (f.type === 'accessor') return 'object'
  if (f.type === 'boolean') return 'bool'
  if (f.rawType === 'float' || (f.scale !== undefined && f.scale !== 1)) return 'float'
  return 'int'
}

/** The input pin of a field that has one (`input` optional / required): same id, the field's value stands in while unwired. */
const pinOfField = (f: Field): Pin => ({
  kind: 'pin',
  id: f.id,
  alias: f.alias,
  ...(f.description !== undefined ? { description: f.description } : {}),
  value: valueKindOf(f),
  required: f.input === 'required',
  overrides: f.id,
  writes: false,
  hints: f.hints,
  field: f.id,
})

const pin = (raw: Json): Pin =>
  defined({
    kind: 'pin' as const,
    id: str(raw.id) ?? '',
    alias: str(raw.alias) ?? str(raw.id) ?? '',
    description: str(raw.description),
    value: (str(raw.value) ?? 'any') as ValueKind,
    required: bool(raw.required) ?? false,
    flow: raw.flow === 'conditional' ? ('conditional' as const) : undefined,
    overrides: str(raw.overrides),
    writes: bool(raw.writes) ?? false,
    hints: hints(raw.hints),
  }) as Pin

const pinSpecs = (v: unknown): readonly PinSpec[] =>
  list(v).flatMap((p): PinSpec[] => {
    const raw = obj(p)
    if (!raw) return []
    const group = obj(raw.group)
    if (!group) return [pin(raw)]
    return [
      defined({
        kind: 'group' as const,
        alias: str(group.alias) ?? 'Pin',
        description: str(group.description),
        value: (str(group.value) ?? 'any') as ValueKind,
        min: num(group.min) ?? 0,
        max: num(group.max) ?? 1,
        label: group.label === 'shown' ? ('shown' as const) : ('hidden' as const),
        required: bool(group.required) ?? false,
        flow: group.flow === 'conditional' ? ('conditional' as const) : undefined,
      }) as PinSpec,
    ]
  })

const activation = (v: unknown): Activation | undefined => {
  const o = obj(v)
  const kind = str(o?.kind)
  const text = str(o?.text)
  if (!o || !text || (kind !== 'enabled' && kind !== 'enable-rising' && kind !== 'triggered')) return undefined
  return { kind, text }
}

const signal = (v: unknown): Signal | undefined => {
  const o = obj(v)
  const alias = str(o?.alias)
  const description = str(o?.description)
  if (!o || !alias || !description) return undefined
  return defined({ alias, description, required: bool(o.required) })
}

/** Output pins for what a command returns: one per field of a struct, else one pin for the single value. */
const outputsOfReturns = (returns: unknown): readonly PinSpec[] => {
  const r = obj(returns)
  if (!r) return []
  const fields = list(r.fields).flatMap((f) => (obj(f) ? [obj(f) as Json] : []))
  const kindOf = (type: unknown): ValueKind => (type === 'float' ? 'float' : type === 'bool' ? 'bool' : 'int')
  if (fields.length > 0) {
    return fields.map(
      (f): Pin => ({
        kind: 'pin',
        id: str(f.id) ?? 'result',
        alias: str(f.label) ?? str(f.alias) ?? str(f.id) ?? 'Result',
        ...(str(f.description) !== undefined ? { description: str(f.description) as string } : {}),
        value: kindOf(f.raw_type),
        required: false,
        writes: false,
        hints: [],
      }),
    )
  }
  return [
    {
      kind: 'pin',
      id: 'result',
      alias: str(r.label) ?? 'Result',
      ...(str(r.description) !== undefined ? { description: str(r.description) as string } : {}),
      value: kindOf(r.type),
      required: false,
      writes: false,
      hints: [],
    },
  ]
}

/** The argument fields of a side of a command: `args.fields`, or `args` itself when it is a list. */
const sideArgs = (side: Json | undefined): readonly Json[] => {
  const args = side?.args
  const fields = Array.isArray(args) ? args : list(obj(args)?.fields)
  return fields.flatMap((f) => (obj(f) ? [obj(f) as Json] : []))
}

/**
 * What a read side returns, without the key it echoes: a mirror read answers the write's arguments, its key (the pin) first, and
 * the board hands the block only what follows the key (vm_block_device.c).
 */
const returnsWithoutKey = (side: Json | undefined): unknown => {
  const returns = obj(side?.returns)
  const keys = new Set(sideArgs(side).map((a) => str(a.id)))
  if (!returns || !Array.isArray(returns.fields) || keys.size === 0) return returns
  return { ...returns, fields: returns.fields.filter((f) => !keys.has(str(obj(f)?.id))) }
}

/** `cmd_dir_e` by side (enums.json): the byte that picks the read or the write of a command. */
const dirOf = (enums: EnumTables, side: 'read' | 'write'): number | undefined =>
  enums.cmd_dir_e?.members.find((m) => m.name === (side === 'read' ? 'CMD_DIR_READ' : 'CMD_DIR_WRITE'))?.value

interface Resolved {
  readonly operation: Operation
  readonly fields: readonly Field[]
  readonly outputs: readonly PinSpec[]
}

/** `SYS_DEVICE_CONTRACT_OWN`: the contract number of a device's own commands (enums.json, group `device_contract`). */
const ownContractId = (enums: EnumTables): number | undefined => enums.device_contract?.members.find((m) => m.name === 'SYS_DEVICE_CONTRACT_OWN')?.value

/** A variant's command, its arguments as fields (with the variant's per-argument overrides) and its returned values as outputs. */
const resolveOperation = (
  source: 'contract' | 'device',
  of: string,
  contractId: number | undefined,
  commands: readonly GeneratedCommand[],
  commandId: string,
  sideId: string | undefined,
  argOverrides: Json | undefined,
  enums: EnumTables,
): Resolved | string => {
  const command = commands.find((c) => c.id === commandId)
  if (!command) return `${source} '${of}' has no command '${commandId}'`
  const op = num(command.op)
  if (op === undefined) return `command '${commandId}' of ${source} '${of}' has no op number`
  if (contractId === undefined) return `${source} '${of}' has no contract id to build the op code of '${commandId}' from`
  const sides = (['read', 'write'] as const).filter((s) => obj(command[s]))
  const sideName = sideId === 'read' || sideId === 'write' ? sideId : sides.length === 1 ? sides[0] : undefined
  if (!sideName) return `command '${commandId}' of ${source} '${of}' has a read and a write: the variant names neither (\`dir\`)`
  const side = obj(command[sideName])
  if (!side) return `command '${commandId}' of ${source} '${of}' has no ${sideName} side`
  const dir = dirOf(enums, sideName)
  if (dir === undefined) return `enums.json has no cmd_dir_e member for a ${sideName}`
  const value = ((contractId << 8) | op) & 0xffff
  const fields = sideArgs(side).map((arg) => {
    const base = field(arg, enums, obj(argOverrides?.[str(arg.id) ?? '']))
    // A command argument is an input by default (the descriptor schema: "an argument not listed behaves as the contract says,
    // with input 'optional'"), unless the variant fixes it or says otherwise.
    const mode = inputMode(obj(argOverrides?.[base.id])?.input) ?? (base.fixed !== undefined ? 'none' : 'optional')
    return { ...base, input: mode } satisfies Field
  })
  return { operation: { source, of, command: commandId, dir, value }, fields, outputs: sideName === 'read' ? outputsOfReturns(returnsWithoutKey(side)) : outputsOfReturns(side.returns) }
}

const category = (v: unknown): Category => {
  const text = str(v)
  return text === 'flow' || text === 'logic' || text === 'data' || text === 'time' || text === 'device' || text === 'system' ? text : 'system'
}

const integration = (v: unknown): Integration => (v === 'contract' || v === 'custom' ? v : 'core')

/** Bytes the fields reach, rounded up to 4: a floor for a descriptor that does not publish `state_len`. */
const derivedLength = (fields: readonly Field[]): number => {
  const end = fields.reduce((n, f) => Math.max(n, (f.offset ?? 0) + (f.size ?? 0)), 0)
  return (end + 3) & ~3
}

const blockType = (raw: GeneratedBlock, input: GeneratedInput, warnings: string[]): BlockType => {
  const id = raw.id
  const kind = integration(raw.integration)
  const device = obj(raw.device)
  const fields = list(raw.fields).flatMap((f) => (obj(f) ? [field(obj(f) as Json, input.enums)] : []))

  const contractId = str(device?.contract)
  const deviceType = str(device?.device_type)
  const contract = contractId ? input.contracts.find((c) => c.id === contractId) : undefined
  const deviceDescriptor = deviceType ? input.devices.find((d) => d.id === deviceType) : undefined
  if (contractId && !contract) warnings.push(`${id}: no contract '${contractId}' in generated/contracts`)
  if (deviceType && !deviceDescriptor) warnings.push(`${id}: no device '${deviceType}' in generated/devices`)

  const variants = list(raw.variants).flatMap((v): Variant[] => {
    const rawVariant = obj(v)
    if (!rawVariant) return []
    const variantId = str(rawVariant.id) ?? ''
    const opId = str(rawVariant.op)
    const own = list(rawVariant.fields).flatMap((f) => (obj(f) ? [field(obj(f) as Json, input.enums)] : []))
    let resolved: Resolved | undefined
    let unsupported: string | undefined
    if (opId !== undefined) {
      const result = contract
        ? resolveOperation('contract', contract.id, num(contract.contract_id), contract.commands ?? [], opId, str(rawVariant.dir), obj(rawVariant.args), input.enums)
        : deviceDescriptor
          ? resolveOperation('device', deviceDescriptor.id, ownContractId(input.enums), deviceDescriptor.commands ?? [], opId, str(rawVariant.dir), obj(rawVariant.args), input.enums)
          : `no contract or device to resolve '${opId}' in`
      if (typeof result === 'string') unsupported = result
      else resolved = result
    } else if (kind !== 'core') {
      unsupported = 'the descriptor names no operation (`op`) for this variant'
    }
    const explicitOutputs = pinSpecs(rawVariant.outputs)
    return [
      defined({
        id: variantId,
        alias: str(rawVariant.alias) ?? variantId,
        description: str(rawVariant.description),
        header: obj(rawVariant.header) ? defined({ subtitle: str(obj(rawVariant.header)?.subtitle) }) : undefined,
        body: rawVariant.body !== undefined ? list(rawVariant.body).flatMap((b) => (typeof b === 'string' ? [b] : [])) : undefined,
        activation: activation(rawVariant.activation),
        operation: resolved?.operation,
        fields: [...(resolved?.fields ?? []), ...own],
        inputs: pinSpecs(rawVariant.inputs),
        outputs: explicitOutputs.length > 0 ? explicitOutputs : (resolved?.outputs ?? []),
        unsupported,
      }) as Variant,
    ]
  })

  // The descriptor publishes the firmware's `.state_len` (checked against the C by templates/crosscheck.py). A descriptor of an
  // older schema without it gets what its fields reach, and a warning.
  let stateLength = num(raw.state_len)
  if (stateLength === undefined) {
    stateLength = derivedLength(fields)
    warnings.push(`${id}: the descriptor has no state_len; assuming ${stateLength} bytes from its fields`)
  }

  const act = activation(raw.activation) ?? { kind: 'enabled' as const, text: '' }
  const header = obj(raw.header)
  return defined({
    id,
    wireId: raw.wire_id,
    alias: str(raw.alias) ?? id,
    description: str(raw.description) ?? '',
    category: category(raw.category),
    order: num(raw.order) ?? 0,
    tags: list(raw.tags).flatMap((t) => (typeof t === 'string' ? [t] : [])),
    integration: kind,
    activation: act,
    header: defined({ subtitle: str(header?.subtitle) }),
    body: list(raw.body).flatMap((b) => (typeof b === 'string' ? [b] : [])),
    editor: str(raw.editor),
    opcodes: opcodes(raw.opcodes),
    enable: signal(raw.enable),
    eno: signal(raw.eno),
    span: obj(raw.span) ? { offset: num(obj(raw.span)?.offset) ?? 0 } : undefined,
    device: device ? defined({ contract: contractId, deviceType }) : undefined,
    inputs: [...pinSpecs(raw.inputs), ...fields.filter((f) => f.input !== 'none').map(pinOfField)],
    outputs: pinSpecs(raw.outputs),
    fields,
    variants,
    stateLength,
    stateVariable: bool(raw.state_variable) ?? false,
    guide: str(raw.guide),
  }) as BlockType
}

/** The catalog from the generated descriptors, in palette order (`category`, then `order`, then name). */
export const blocksFromGenerated = (input: GeneratedInput): CatalogResult => {
  const warnings: string[] = []
  const seen = new Map<number, string>()
  const blocks = input.blocks.map((raw) => {
    const block = blockType(raw, input, warnings)
    const clash = seen.get(block.wireId)
    if (clash) warnings.push(`${block.id}: wire_id ${block.wireId} is also ${clash}`)
    seen.set(block.wireId, block.id)
    return block
  })
  const rank: Record<Category, number> = { flow: 0, logic: 1, data: 2, time: 3, device: 4, system: 5 }
  blocks.sort((a, b) => rank[a.category] - rank[b.category] || a.order - b.order || a.alias.localeCompare(b.alias))
  return { blocks, warnings }
}
