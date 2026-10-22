import type { EnumTables } from '@engine/generated/wire'
import type { ServiceCatalog } from '../catalog/services'
import type { ErrorField, ErrorInfo, IdKindInfo, LinkDefinitions } from '../definitions'
import { bundledBlockTypes, bundledEnums, bundledLinkDefinitions, bundledServiceCatalog, errorCatalog, errorIdKinds } from '../definitions'
import { decodeErrorRecord } from '../protocol/errors'
import type { ErrorNode, ErrorRecord } from '../protocol/errors'

/*
 * An error record read for a user: every node of the chain with its message (the template of errors.json, its `{field}`s
 * filled in), and every instance the error is about, as references (`Ref`) the app can attach the error to: a device, a pin
 * or a channel of it, a variable, an accessor, a block and its pins, an action, ...
 *
 * Everything comes from errors.json: what a field refers to (`id_kind`, or `id_kind_by` the value of another field), which
 * kinds are numbered within another (`scope`: a pin within a device; the field `of`, else the one field of that kind in the
 * same node, else the nearest one before it in the chain or in the request), which nodes are context (`context`: where the
 * error is, not what went wrong), the enum group of a field (`enum_ref`) and how a bare number is shown (`format`). The
 * names of instances come from the descriptors (device types, commands, router keys, block types, error tags) and from the
 * app (`RefResolver`: its devices and its program, errors/program.ts). Nothing here names an error, a field or a kind.
 */

/** An instance an error is about. `scope`: the instance it is numbered within (the device of a pin). */
export interface Ref {
  readonly kind: string
  readonly id: number
  readonly scope?: Ref
}

/** The key of a reference: `device:3`, `device:3/pin:5`, `vm_block:2/vm_block_input:0`. */
export const refKey = (ref: Ref): string => `${ref.scope ? `${refKey(ref.scope)}/` : ''}${ref.kind}:${ref.id}`

/** `ref` is `within` or inside it (`device:3/pin:5` is within `device:3`). */
export const refWithin = (ref: Ref, within: Ref | string): boolean => {
  const key = refKey(ref)
  const prefix = typeof within === 'string' ? within : refKey(within)
  return key === prefix || key.startsWith(`${prefix}/`)
}

/** What the app makes of a reference: the name a user reads (never an internal id) and what to point at in the app. */
export interface RefTarget {
  /** Undefined: the descriptors' name, else the kind's alias and the number (`Pin 5`). */
  readonly label?: string
  /** The app's own object (its variable, block, device, pin): whatever the resolver returns, for the UI to highlight. */
  readonly target?: unknown
}

/** The app's names for instances; undefined: not one it knows (the descriptors or the kind's alias and number stand in). */
export type RefResolver = (ref: Ref) => RefTarget | undefined

/** Several resolvers, the first that knows a reference wins. */
export const combineResolvers =
  (...resolvers: readonly (RefResolver | undefined)[]): RefResolver =>
  (ref) => {
    for (const resolve of resolvers) {
      const found = resolve?.(ref)
      if (found) return found
    }
    return undefined
  }

export interface FieldReport {
  readonly name: string
  /** The raw value (a number, or text for a `chars` field). */
  readonly value: number | string
  /** As a user reads it: the instance's name, the enum member's alias, the number with its unit. */
  readonly text: string
  readonly ref?: Ref
}

export interface NodeReport {
  readonly tag: number
  /** `ERR_...`, or the tag in hex when the firmware's errors.json does not have it. */
  readonly name: string
  readonly module?: string
  readonly severity: number
  readonly context: boolean
  /** The message template with the fields filled in. */
  readonly message: string
  /** What a user can do about it, the fields filled in (errors.json `hint`). */
  readonly hint?: string
  readonly description?: string
  readonly fields: readonly FieldReport[]
  /** The instances this node names, scopes filled in from the chain. */
  readonly refs: readonly Ref[]
  /** errors.json has the tag. */
  readonly known: boolean
}

export interface ErrorReport {
  readonly record: ErrorRecord
  /** The highest severity in the chain. */
  readonly severity: number
  /** The root cause's message. */
  readonly message: string
  /** The context nodes' messages, outermost first (`In device 3`, `In IO LEVEL`). */
  readonly where: readonly string[]
  readonly chain: readonly NodeReport[]
  readonly cause?: NodeReport
  /** The command that failed, when the record carries it (`src` command). */
  readonly command?: { readonly label: string; readonly refs: readonly Ref[] }
  /** Every instance the error is about (the request's, then each node's), once each. */
  readonly refs: readonly Ref[]
  /** The same error happening again has the same key: the chain, without the command's link and sequence. */
  readonly key: string
}

export interface InterpreterOptions {
  readonly defs?: LinkDefinitions
  readonly errors?: ReadonlyMap<number, ErrorInfo>
  readonly kinds?: ReadonlyMap<string, IdKindInfo>
  readonly enums?: EnumTables
  readonly services?: ServiceCatalog
  /** Block type wire id -> its name (the block descriptors). */
  readonly blockTypes?: ReadonlyMap<number, string>
  /** The app's names: devices, the program's variables and blocks (errors/program.ts). */
  readonly resolve?: RefResolver
}

const hex = (n: number) => `0x${n.toString(16).toUpperCase()}`

/** Reads error records for a user, from the bundled descriptors and the app's names. */
export class ErrorInterpreter {
  readonly defs: LinkDefinitions
  readonly errors: ReadonlyMap<number, ErrorInfo>
  readonly kinds: ReadonlyMap<string, IdKindInfo>
  readonly enums: EnumTables
  readonly services: ServiceCatalog
  readonly blockTypes: ReadonlyMap<number, string>
  resolve: RefResolver | undefined

  constructor(options: InterpreterOptions = {}) {
    this.defs = options.defs ?? bundledLinkDefinitions()
    this.errors = options.errors ?? errorCatalog()
    this.kinds = options.kinds ?? errorIdKinds()
    this.enums = options.enums ?? bundledEnums()
    this.services = options.services ?? bundledServiceCatalog()
    this.blockTypes = options.blockTypes ?? bundledBlockTypes()
    this.resolve = options.resolve
  }

  /** The record of an ERROR answer or of the errors stream, read; undefined when the bytes are not a record. */
  read(data: Uint8Array): ErrorReport | undefined {
    const record = decodeErrorRecord(this.defs, data, this.errors)
    return record && this.interpret(record)
  }

  interpret(record: ErrorRecord): ErrorReport {
    const command = record.command ? this.command(record.command.request) : undefined
    const seen: Ref[] = [...(command?.refs ?? [])]
    const chain = record.chain.map((node) => {
      const report = this.node(node, seen)
      seen.push(...report.refs)
      return report
    })
    const cause = chain.at(-1)
    const refs = new Map<string, Ref>()
    for (const ref of seen) if (!refs.has(refKey(ref))) refs.set(refKey(ref), ref)
    return {
      record,
      severity: Math.max(0, ...chain.map((n) => n.severity)),
      message: cause?.message ?? 'Error',
      where: chain.slice(0, -1).filter((n) => n.context).map((n) => n.message),
      chain,
      ...(cause ? { cause } : {}),
      ...(command ? { command } : {}),
      refs: [...refs.values()],
      key: `${record.chainCut ? 'cut|' : ''}${record.chain.map((n) => `${n.tag.toString(16)}:${[...n.payload].map((b) => b.toString(16).padStart(2, '0')).join('')}`).join('|')}`,
    }
  }

  /** The name a user reads for a reference. */
  label(ref: Ref): string {
    return this.resolve?.(ref)?.label ?? this.describe(ref) ?? this.fallback(ref)
  }

  /** What the app points at for a reference, when its resolver knows it. */
  target(ref: Ref): unknown {
    return this.resolve?.(ref)?.target
  }

  private fallback(ref: Ref): string {
    const kind = this.kinds.get(ref.kind)
    return `${kind?.alias ?? ref.kind} ${kind?.format === 'hex' ? hex(ref.id) : ref.id}`
  }

  /** Names the descriptors give: device types, device commands, router keys, VM commands, contracts, block types, errors. */
  private describe(ref: Ref): string | undefined {
    const s = this.services
    switch (ref.kind) {
      case 'device_type':
        return [...s.devices.values()].find((d) => d.typeId === ref.id)?.label
      case 'contract':
        return s.byContractId.get(ref.id)?.label
      case 'router_key':
        return s.byKey.get(ref.id)?.label ?? this.enums.router_key?.members.find((m) => m.value === ref.id)?.alias
      case 'vm_op': {
        const vm = s.byKey.get(this.defs.keys.vm!)
        const command = vm && [...vm.commands.values()].find((c) => c.op === ref.id)
        return command && `${vm.label}: ${command.label}`
      }
      case 'device_op': {
        const contract = s.byContractId.get(ref.id >> 8)
        const command = contract && [...contract.commands.values()].find((c) => c.op === (ref.id & 0xff))
        return command && `${contract.label}: ${command.label}`
      }
      case 'vm_block_type':
        return this.blockTypes.get(ref.id)
      case 'error_tag':
        return this.errors.get(ref.id)?.name
      default:
        return undefined
    }
  }

  /** The command of a record: `[key][dir][op][args]`, or `[device key][dir][id u16][op u16][args]` for a device. */
  private command(request: Uint8Array): { label: string; refs: Ref[] } | undefined {
    const key = request[0]
    if (key === undefined) return undefined
    if (key === this.defs.keys.device && request.length >= 6) {
      const id = request[2]! | (request[3]! << 8)
      const device: Ref = { kind: 'device', id }
      const op: Ref = { kind: 'device_op', id: request[4]! | (request[5]! << 8), scope: device }
      return { label: `${this.label(op)} (${this.label(device)})`, refs: [device, op] }
    }
    const target: Ref = { kind: 'router_key', id: key }
    const entry = this.services.byKey.get(key)
    const command = entry && request.length >= 3 ? [...entry.commands.values()].find((c) => c.op === request[2]) : undefined
    return { label: command ? `${entry!.label}: ${command.label}` : this.label(target), refs: [target] }
  }

  private node(node: ErrorNode, before: readonly Ref[]): NodeReport {
    const info = node.info
    if (!info) {
      return { tag: node.tag, name: hex(node.tag), severity: node.severity, context: false, message: `Error ${hex(node.tag)}`, fields: [], refs: [], known: false }
    }
    const values = node.fields ?? {}
    const byName = new Map(info.fields.map((f) => [f.name, f]))
    const kindOf = (field: ErrorField): string | undefined => {
      if (field.id_kind) return field.id_kind
      if (!field.id_kind_by) return undefined
      const selector = values[field.id_kind_by.field]
      return selector === undefined ? undefined : field.id_kind_by.kinds[String(selector)]
    }

    // unscoped references first, so a scoped one can find its instance in the same node
    const refs = new Map<string, Ref>()
    const own = (field: ErrorField, kind: string): Ref | undefined => {
      const value = values[field.name]
      if (typeof value !== 'number') return undefined
      const scope = this.kinds.get(kind)?.scope
      if (!scope) return { kind, id: value }
      const holder = field.of ? byName.get(field.of) : info.fields.find((f) => f !== field && kindOf(f) === scope && typeof values[f.name] === 'number')
      const inNode = holder && refs.get(holder.name)
      const outer = inNode ?? [...before].reverse().find((r) => r.kind === scope)
      return outer ? { kind, id: value, scope: outer } : { kind, id: value }
    }
    const kinds = info.fields.map((f) => [f, kindOf(f)] as const)
    for (const [field, kind] of kinds) if (kind && !this.kinds.get(kind)?.scope) set(refs, field, own(field, kind))
    for (const [field, kind] of kinds) if (kind && this.kinds.get(kind)?.scope) set(refs, field, own(field, kind))

    const fields = info.fields.flatMap((field): FieldReport[] => {
      const value = values[field.name]
      if (value === undefined) return []
      const ref = refs.get(field.name)
      return [{ name: field.name, value, text: this.text(field, value, ref), ...(ref ? { ref } : {}) }]
    })
    const texts = new Map(fields.map((f) => [f.name, f.text]))
    const fill = (template: string) => template.replace(/\{([^{}]*)\}/g, (all, name: string) => texts.get(name) ?? all)
    const message = fill(info.message)
    return {
      tag: node.tag,
      name: info.name,
      module: info.module,
      severity: node.severity,
      context: info.context,
      message,
      ...(info.hint ? { hint: fill(info.hint) } : {}),
      ...(info.description ? { description: info.description } : {}),
      fields,
      refs: [...refs.values()],
      known: true,
    }
  }

  private text(field: ErrorField, value: number | string, ref: Ref | undefined): string {
    if (typeof value === 'string') return value
    if (ref) return this.label(ref)
    if (field.enum_ref) {
      const member = this.enums[field.enum_ref]?.members.find((m) => m.value === value)
      if (member) return member.alias ?? member.name
    }
    const shown = field.type === 'f32' ? Number(value.toPrecision(7)) : value  // a float32 has about 7 digits
    return field.unit ? `${shown} ${field.unit}` : String(shown)
  }
}

const set = (refs: Map<string, Ref>, field: ErrorField, ref: Ref | undefined) => {
  if (ref) refs.set(field.name, ref)
}
