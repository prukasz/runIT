import { concretePins, formatPath, resolvePath, variantOf } from '@engine'
import type { BlockInstance, BlockType, Catalog, ConcretePin, ConcretePins, Diagnostic, ExpressionLanguage, Field, FieldValue, ObjectPath, Project, Source, ValueKind, Variant } from '@engine'

/*
 * The Code view's blocks, pure: new ids, what a block is called and what its pins read, the words of its header, the wires
 * between blocks, connecting a source to a pin and taking it off, which sources fit which pins, removing and copying blocks.
 * The blocks themselves are the engine's (`BlockInstance`, `model/program`); the canvas draws them (canvas/), the details edit
 * them (BlockDetails.tsx).
 */

/** A pin a source can go on: an input, the block's "Run when" (its enables; `index`: one of them), or an output (a variable it writes). */
export type PinTarget =
  | { readonly block: string; readonly side: 'in'; readonly index: number }
  | { readonly block: string; readonly side: 'en'; readonly index?: number }
  | { readonly block: string; readonly side: 'out'; readonly index: number }

/** What a block sends on: an output pin by position, or its enable-out ("When done"). */
export type BlockPin = number | 'eno'

/** A wire between two blocks: a block's output or enable-out read by another block's input or enable. */
export interface Wire {
  readonly from: { readonly block: string; readonly pin: BlockPin }
  readonly to: PinTarget
}

/** What the names and words of a block are made of: the project, the catalog, and how the app names devices, pins, events and actions. */
export interface BlockContext {
  readonly catalog: Catalog
  readonly project: Project
  readonly deviceName: (id: number) => string | undefined
  readonly pinName: (device: number, pin: number) => string
  /** An event of a device (`code`, `contract_*.json` events) by its label; device undefined: of any device. */
  readonly eventName: (device: number | undefined, code: number) => string | undefined
  readonly actionName: (id: number) => string | undefined
  readonly language: (type: string) => ExpressionLanguage | undefined
  /** The board has the device suspended now (safe state, a fault, a SUSPEND): its blocks say so. Unknown (no board): false. */
  readonly deviceSuspended?: (id: number) => boolean
}

/* ---- ids and names ---- */

/** `<type><n>`, the lowest n no block has (`periodic1`, `expr3`). Ids are the project's and never shown. */
export const newBlockId = (blocks: readonly BlockInstance[], type: string): string => {
  const taken = new Set(blocks.map((b) => b.id))
  for (let n = 1; ; n++) if (!taken.has(`${type}${n}`)) return `${type}${n}`
}

/**
 * What the user calls a block: its name, else its type's (`Every`). Two blocks that would read alike are numbered by their place
 * in the project (`Every 2`), so a label never needs the id.
 */
export const blockName = (catalog: Catalog, blocks: readonly BlockInstance[], id: string): string => {
  const block = blocks.find((b) => b.id === id)
  if (!block) return 'a removed block'
  const base = (b: BlockInstance) => b.name?.trim() || catalog.get(b.type)?.alias || b.type
  const own = base(block)
  if (block.name?.trim()) return own
  const alike = blocks.filter((b) => !b.name?.trim() && base(b) === own)
  return alike.length > 1 ? `${own} ${alike.indexOf(block) + 1}` : own
}

/** The block's pins with its groups expanded, or undefined for a type the catalog does not know. */
export const pinsOf = (catalog: Catalog, block: BlockInstance): ConcretePins | undefined => {
  const type = catalog.get(block.type)
  return type ? concretePins(type, block) : undefined
}

/** What an output of a block is called (`Result`), or what its enable-out means (`Tick`, `When done`). */
export const blockPinName = (catalog: Catalog, block: BlockInstance | undefined, pin: BlockPin): string => {
  if (!block) return pin === 'eno' ? 'When done' : 'Output'
  const type = catalog.get(block.type)
  if (pin === 'eno') return type?.eno?.alias ?? 'When done'
  return pinsOf(catalog, block)?.outputs[pin]?.pin.alias ?? `Output ${pin + 1}`
}

/** A source as the user reads it: a variable by its path, another block's output as `Every.Tick`. */
export const sourceLabel = (ctx: Pick<BlockContext, 'catalog' | 'project'>, source: Source): string => {
  if (source.kind === 'variable') return formatPath(ctx.project.variables, source.path)
  const block = ctx.project.blocks.find((b) => b.id === source.block)
  // An output that writes a variable is that variable: the board reads the same object.
  const written = source.kind === 'output' ? block?.outputs[source.pin] : undefined
  if (written) return formatPath(ctx.project.variables, written.path)
  return `${blockName(ctx.catalog, ctx.project.blocks, source.block)}.${blockPinName(ctx.catalog, block, source.kind === 'eno' ? 'eno' : source.pin)}`
}

/** A label short enough for a chip: whole when it fits, else the end that names it (`…temperature`, `…gains[2]`). */
export const chipLabel = (label: string, max = 16): string => {
  if (label.length <= max) return label
  const last = label.slice(label.lastIndexOf('.') + 1)
  return last.length + 1 <= max ? `…${last}` : `…${label.slice(-(max - 1))}`
}

/* ---- sources and pins ---- */

export const sameSource = (a: Source | null | undefined, b: Source | null | undefined): boolean => JSON.stringify(a ?? null) === JSON.stringify(b ?? null)

export const sameTarget = (a: PinTarget | undefined, b: PinTarget | undefined): boolean =>
  !!a && !!b && a.block === b.block && a.side === b.side && (a.index ?? -1) === (b.index ?? -1)

/** The source a block pin stands for, read by another block. */
export const blockSource = (block: string, pin: BlockPin): Source => (pin === 'eno' ? { kind: 'eno', block } : { kind: 'output', block, pin })

/** The variable a pin holds (an input reading one, an enable, an output writing one), if it holds a variable. */
export const pinVariable = (block: BlockInstance, target: PinTarget): ObjectPath | undefined => {
  if (target.side === 'out') return block.outputs[target.index]?.path ?? undefined
  const source = target.side === 'in' ? block.inputs[target.index] : target.index === undefined ? undefined : block.enables[target.index]
  return source?.kind === 'variable' ? source.path : undefined
}

/**
 * A source onto a pin: an input takes it in place of what it had, the enables get one more (once), an output writes the variable
 * (only a variable goes on an output). Returns the block unchanged when the source does not go there.
 */
export const connect = (block: BlockInstance, target: PinTarget, source: Source): BlockInstance => {
  if (target.side === 'en') return block.enables.some((s) => sameSource(s, source)) ? block : { ...block, enables: [...block.enables, source] }
  if (target.side === 'out') {
    if (source.kind !== 'variable') return block
    const outputs = Array.from({ length: Math.max(block.outputs.length, target.index + 1) }, (_, i) => block.outputs[i] ?? null)
    outputs[target.index] = { path: source.path }
    return { ...block, outputs }
  }
  const inputs = Array.from({ length: Math.max(block.inputs.length, target.index + 1) }, (_, i) => block.inputs[i] ?? null)
  inputs[target.index] = source
  return { ...block, inputs }
}

/**
 * `connect`, keeping the flow: an output of another block wired onto an input of a block with no Run when also gives it that block's
 * When done, so it runs on the passes the value is made, as the blocks before it do. Not when the source block reads this one (that
 * would close a loop), and not for a block that sends something when its enable falls (When disabled: Brake, Coast): it would send
 * it after every pass of the source.
 */
export const connectWithFlow = (blocks: readonly BlockInstance[], block: BlockInstance, target: PinTarget, source: Source): BlockInstance => {
  const next = connect(block, target, source)
  if (target.side !== 'in' || source.kind !== 'output' || source.block === block.id || block.enables.length > 0 || next === block) return next
  const producer = blocks.find((b) => b.id === source.block)
  const readsThis = (s: Source | null) => !!s && s.kind !== 'variable' && s.block === block.id
  if (!producer || [...producer.inputs, ...producer.enables].some(readsThis)) return next
  const idle = block.values?.idle
  if (typeof idle === 'number' && idle !== 0) return next
  return { ...next, enables: [{ kind: 'eno', block: source.block }] }
}

/** Takes off what a pin holds: an input or an output back to nothing, one enable (by position) or all of them. */
export const disconnect = (block: BlockInstance, target: PinTarget): BlockInstance => {
  if (target.side === 'en') return { ...block, enables: target.index === undefined ? [] : block.enables.filter((_, i) => i !== target.index) }
  if (target.side === 'out') return { ...block, outputs: block.outputs.map((t, i) => (i === target.index ? null : t)) }
  return { ...block, inputs: block.inputs.map((s, i) => (i === target.index ? null : s)) }
}

/** Every wire between two blocks of the project: inputs and enables that read another block's output or enable-out. */
export const wiresOf = (blocks: readonly BlockInstance[]): Wire[] => {
  const ids = new Set(blocks.map((b) => b.id))
  const from = (s: Source | null | undefined) => (s && s.kind !== 'variable' && ids.has(s.block) ? { block: s.block, pin: s.kind === 'eno' ? ('eno' as const) : s.pin } : undefined)
  return blocks.flatMap((block) => [
    ...block.inputs.flatMap((s, index): Wire[] => {
      const f = from(s)
      return f ? [{ from: f, to: { block: block.id, side: 'in', index } }] : []
    }),
    ...block.enables.flatMap((s, index): Wire[] => {
      const f = from(s)
      return f ? [{ from: f, to: { block: block.id, side: 'en', index } }] : []
    }),
  ])
}

/* ---- what fits where ---- */

/** What travels: numbers and booleans cast to each other; a whole object does not. */
export type Sent = 'number' | 'bool' | 'object'

const sentOfKind = (value: ValueKind): Sent => (value === 'object' || value === 'cell' ? 'object' : value === 'bool' || value === 'flow' ? 'bool' : 'number')

/** What a source sends: an output by its pin's kind, an enable-out a boolean, a variable by what its path names. */
export const sentBy = (ctx: Pick<BlockContext, 'catalog' | 'project'>, source: Source): Sent => {
  if (source.kind === 'eno') return 'bool'
  if (source.kind === 'output') {
    const block = ctx.project.blocks.find((b) => b.id === source.block)
    const pin = block && pinsOf(ctx.catalog, block)?.outputs[source.pin]
    return pin ? sentOfKind(pin.pin.value) : 'number'
  }
  const resolved = resolvePath(ctx.project.variables, source.path)
  if (!resolved.ok) return 'number'
  // Below a run-time folder the board decides what it is: it fits a value pin.
  if (resolved.target.unchecked) return 'number'
  const { variable, element } = resolved.target
  if (variable.kind !== 'value') return 'object'
  if (element === null && variable.count > 1) return 'object'
  return variable.type === 'bool' ? 'bool' : 'number'
}

/** Whether a pin takes what is sent: the enables a boolean or a number, an input by its kind, an output only a variable. */
export const fits = (pin: ConcretePin | undefined, side: PinTarget['side'], sent: Sent, source: Source): boolean => {
  if (side === 'en') return sent !== 'object'
  if (side === 'out') return source.kind === 'variable' && sent !== 'object'
  if (!pin) return false
  if (pin.pin.value === 'object') return true
  // A cell names an element of a folder: only a variable path can.
  if (pin.pin.value === 'cell') return source.kind === 'variable'
  return sent !== 'object'
}

/* ---- the header ---- */

/** One piece of a header: the descriptor's own words, or a value filled in (shown in italics). */
export interface HeadPart {
  readonly text: string
  readonly value?: boolean
}

const numberText = (value: number, unit?: string): string => `${Number.isInteger(value) ? value : Number(value.toFixed(3))}${unit ? ` ${unit}` : ''}`

/** A field's value as the header words it: an option's alias, a device or pin by its name, a number with its unit. */
export const fieldText = (ctx: BlockContext, block: BlockInstance, field: Field): string => {
  const raw: FieldValue | undefined = field.fixed ?? block.values?.[field.id] ?? field.default
  if (raw === undefined) return field.type === 'device' ? 'no device' : '…'
  if (raw === 'any' || (field.any !== undefined && raw === field.any)) return 'any'
  if (field.type === 'enum') {
    const option = field.options?.find((o) => o.value === raw || String(o.value) === String(raw) || o.alias === raw)
    return option?.alias ?? String(raw)
  }
  if (typeof raw === 'boolean') return raw ? 'on' : 'off'
  if (typeof raw !== 'number') return String(raw)
  const deviceOf = (): number | undefined => {
    if (field.deviceField) {
      const value = block.values?.[field.deviceField]
      return typeof value === 'number' ? value : undefined
    }
    return block.device ?? undefined
  }
  if (field.type === 'device') return ctx.deviceName(raw) ?? `device ${raw}`
  if (field.type === 'pin') {
    const device = deviceOf()
    return device === undefined ? `#${raw}` : ctx.pinName(device, raw)
  }
  if (field.type === 'event') return ctx.eventName(deviceOf(), raw) ?? String(raw)
  if (field.type === 'action') return ctx.actionName(raw) ?? `action ${raw}`
  return numberText(raw, field.unit)
}

/** The block's variant, when its type has them. */
export const variantFor = (type: BlockType, block: BlockInstance): Variant | undefined => variantOf(type, block.variant)

/** The formula of an expression block as text with each wired input written as what it reads (`speed * 2 + in1`). */
export const formulaText = (ctx: BlockContext, block: BlockInstance): string | undefined => {
  const language = ctx.language(block.type)
  if (!language) return undefined
  const text = (block.editor as { text?: unknown } | undefined)?.text
  if (typeof text !== 'string' || !text.trim()) return ''
  let out = ''
  let at = 0
  for (const token of language.tokenize(text).tokens) {
    out += text.slice(at, token.from)
    const pin = token.kind === 'input' ? Number(token.text.slice(2)) : -1
    const source = pin >= 0 ? block.inputs[pin] : undefined
    out += source ? sourceLabel(ctx, source) : token.text
    at = token.to
  }
  return out + text.slice(at)
}

/**
 * The header of a block: the descriptor's subtitle (`Every {period} {time_base}`, the variant's own when it has one) with each
 * `{…}` filled in: a field's value (or what its wire reads), a pin's source or target, the variant, the device, the formula.
 * `operation`: a device block is headed by its type's subtitle (`{variant} {pin}`: the operation and what it acts on), not the
 * variant's (`{pin} = {level}`), whose values the canvas shows on the pins already.
 */
export const headline = (ctx: BlockContext, block: BlockInstance, operation = false): HeadPart[] => {
  const type = ctx.catalog.get(block.type)
  if (!type) return [{ text: `Unknown ${block.type}` }]
  const variant = variantFor(type, block)
  const template = operation && type.integration !== 'core' ? type.header.subtitle ?? variant?.header?.subtitle : variant?.header?.subtitle ?? type.header.subtitle
  if (!template) return [{ text: variant ? `${variant.alias}` : type.alias }]
  const pins = concretePins(type, block)
  const fields = [...type.fields, ...(variant?.fields ?? [])]
  const fill = (name: string): string => {
    if (name === 'variant') return variant?.alias ?? type.alias
    if (name === 'device') return block.device === undefined || block.device === null ? 'no device' : ctx.deviceName(block.device) ?? `device ${block.device}`
    if (name === 'expression') {
      const formula = formulaText(ctx, block)
      return formula ? formula : '…'
    }
    const input = pins.inputs.find((p) => p.pin.id === name)
    const source = input ? block.inputs[input.index] : undefined
    if (source) return sourceLabel(ctx, source)
    // An unwired pin that stands for a setting (`overrides`: Set's source, the Value) reads as that setting's value.
    const stands = input?.pin.overrides ? fields.find((f) => f.id === input.pin.overrides) : undefined
    if (stands) return fieldText(ctx, block, stands)
    const field = fields.find((f) => f.id === name)
    if (field) return fieldText(ctx, block, field)
    if (input) return input.pin.alias
    const output = pins.outputs.find((p) => p.pin.id === name)
    if (output) {
      const target = block.outputs[output.index]
      return target ? formatPath(ctx.project.variables, target.path) : output.pin.alias
    }
    return name
  }
  const parts: HeadPart[] = []
  let at = 0
  for (const match of template.matchAll(/\{([^}]+)\}/g)) {
    if (match.index > at) parts.push({ text: template.slice(at, match.index) })
    parts.push({ text: fill(match[1]!), value: true })
    at = match.index + match[0].length
  }
  if (at < template.length) parts.push({ text: template.slice(at) })
  return parts
}

export const headlineText = (parts: readonly HeadPart[]): string => parts.map((p) => p.text).join('')

/** The device line under a device block's pins on the canvas (its device's name), unless its header names the device already. */
export const deviceLine = (ctx: BlockContext, block: BlockInstance): string | undefined => {
  const type = ctx.catalog.get(block.type)
  if (!type || type.integration === 'core' || type.header.subtitle?.includes('{device}')) return undefined
  return block.device === undefined || block.device === null ? 'No device' : ctx.deviceName(block.device) ?? `Device ${block.device}`
}

/* ---- the formula line ---- */

/** One piece of the formula line under an expression block's pins: an input, or the formula's own text. */
export type FormulaPart = { readonly kind: 'in'; readonly pin: number; readonly text: string } | { readonly kind: 'text'; readonly text: string }

/** The formula as pieces: `state` says when there is none yet. Undefined for a block that has no formula. */
export const formulaParts = (ctx: BlockContext, block: BlockInstance): { readonly state: 'ok' | 'empty'; readonly parts: readonly FormulaPart[] } | undefined => {
  const language = ctx.language(block.type)
  if (!language) return undefined
  const text = (block.editor as { text?: unknown } | undefined)?.text
  if (typeof text !== 'string' || !text.trim()) return { state: 'empty', parts: [] }
  const parts: FormulaPart[] = []
  let at = 0
  for (const token of language.tokenize(text).tokens) {
    if (token.from > at) parts.push({ kind: 'text', text: text.slice(at, token.from) })
    parts.push(token.kind === 'input' ? { kind: 'in', pin: Number(token.text.slice(2)), text: token.text } : { kind: 'text', text: token.text })
    at = token.to
  }
  if (at < text.length) parts.push({ kind: 'text', text: text.slice(at) })
  return { state: 'ok', parts }
}

/* ---- removing and copying ---- */

/** Removes blocks; what the others read from them goes too (a wire from a removed block leads nowhere). */
export const removeBlocks = (blocks: readonly BlockInstance[], ids: readonly string[]): BlockInstance[] => {
  const gone = new Set(ids)
  const keeps = (s: Source | null) => !s || s.kind === 'variable' || !gone.has(s.block)
  return blocks.filter((b) => !gone.has(b.id)).map((b) => {
    if (b.inputs.every(keeps) && b.enables.every(keeps)) return b
    return { ...b, inputs: b.inputs.map((s) => (keeps(s) ? s : null)), enables: b.enables.filter(keeps) }
  })
}

/**
 * Copies of blocks under new ids. A copy that read what another copied block sends reads that block's copy, so a group keeps its
 * own wiring; everything else is read as the originals did. The variables the originals write are dropped (a variable has one
 * writer). Returns the copies and the id of each original's copy.
 */
export const copyBlocks = (blocks: readonly BlockInstance[], copies: readonly BlockInstance[]): { readonly added: BlockInstance[]; readonly ids: ReadonlyMap<string, string> } => {
  const ids = new Map<string, string>()
  let all = [...blocks]
  for (const copy of copies) {
    const id = newBlockId(all, copy.type)
    ids.set(copy.id, id)
    all = [...all, { ...copy, id }]
  }
  const remap = (s: Source | null): Source | null => (s && s.kind !== 'variable' && ids.has(s.block) ? { ...s, block: ids.get(s.block)! } : s)
  const added = copies.map((copy) => ({
    ...copy,
    id: ids.get(copy.id)!,
    inputs: copy.inputs.map(remap),
    enables: copy.enables.map((s) => remap(s)!),
    outputs: copy.outputs.map(() => null),
  }))
  return { added, ids }
}

/* ---- groups ---- */

/** How many pins a block's input (or output) group has now, and its range; undefined when the block has no such group. */
export const groupCount = (type: BlockType, block: BlockInstance, side: 'inputs' | 'outputs'): { readonly count: number; readonly min: number; readonly max: number; readonly alias: string } | undefined => {
  const variant = variantFor(type, block)
  const specs = [...type[side], ...(variant?.[side] ?? [])]
  const group = specs.find((s) => s.kind === 'group')
  if (!group || group.kind !== 'group') return undefined
  const count = Math.max(group.min, Math.min(group.max, block.groups?.[side] ?? group.min))
  return { count, min: group.min, max: group.max, alias: group.alias }
}

/** The block with its group grown or shrunk to `count`; what was wired past the new end goes. */
export const withGroupCount = (type: BlockType, block: BlockInstance, side: 'inputs' | 'outputs', count: number): BlockInstance => {
  const now = groupCount(type, block, side)
  if (!now) return block
  const next = Math.max(now.min, Math.min(now.max, count))
  const grouped = { ...block, groups: { ...block.groups, [side]: next } }
  const length = concretePins(type, grouped)[side].length
  return side === 'inputs' ? { ...grouped, inputs: grouped.inputs.slice(0, length) } : { ...grouped, outputs: grouped.outputs.slice(0, length) }
}

/* ---- diagnostics ---- */

/** What the compiler says of one block. */
export const blockDiagnostics = (diagnostics: readonly Diagnostic[], id: string): Diagnostic[] =>
  diagnostics.filter((d) => d.where.kind === 'block' && d.where.id === id)

/** The block an error's target is about (`programRefs`: `{ kind: 'block', block }`). */
export const isBlockTarget = (target: unknown, id: string): boolean =>
  typeof target === 'object' && target !== null && (target as { kind?: unknown }).kind === 'block' && (target as { block?: unknown }).block === id
