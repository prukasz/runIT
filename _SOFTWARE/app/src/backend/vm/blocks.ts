import enumsJson from '@data-structures/enums.json'
import type { EnumMembers } from '../protocol/errors'

/** A state field of a block descriptor (data-structures/vm/blocks/<block>.block.json `state.fields`). */
export interface StateField {
  readonly name: string
  readonly c_type: string
  readonly offset: number
  readonly size: number
  readonly source: 'user' | 'runtime' | 'padding' | 'derived'
  readonly enum_ref?: string
  /** What the value names: `device`, `pin` ... */
  readonly id_kind?: string
  /** The name the user sees. */
  readonly alias?: string
  /** What the app does with it: `device`, `op`, `idle-op`, `const`, `idle-const`; none = a plain setting. */
  readonly role?: string
  readonly array_len?: number
  readonly element_size?: number
  readonly description?: string
}

/** A pin of a block descriptor (`inputs.pins` / `outputs.pins`). */
export interface BlockPin {
  readonly index: number
  readonly name: string
  readonly alias: string
  /** The value kind: `u32`, `f32`, `gate`, `bool`, `object` ... */
  readonly value: string
  readonly description?: string
  /** The setting this input reads while unwired. */
  readonly overrides?: string
  readonly required?: boolean
  readonly hide_when_wired?: boolean
}

export interface BlockPins {
  readonly min: number
  readonly max: number
  readonly pins: readonly BlockPin[]
}

/** An input or output of one operation of a device block. */
export interface OpPin {
  readonly index: number
  readonly name: string
  readonly alias: string
  /** The wire type of an argument (`u8`, `i16` ...); absent for a reading's output. */
  readonly field_type?: string
  /** A reading's value type. */
  readonly value_type?: string
  readonly unit?: string
  readonly id_kind?: string
  readonly device_field?: string
  readonly choices?: readonly { readonly value: number; readonly alias: string }[]
}

/** What a device block can do with its device: a setting (`change`), an action (`edge`) or a reading (`read`). */
export interface BlockOp {
  readonly symbol: string
  /** The value of the block's `op` field. */
  readonly value: number
  readonly alias: string
  readonly description: string
  readonly kind: 'change' | 'edge' | 'read'
  readonly idle_allowed: boolean
  readonly inputs: readonly OpPin[]
  readonly outputs: readonly OpPin[]
  readonly face?: { readonly lead: readonly FacePart[] }
}

/** One part of a block's face line: text, or a reference the app resolves (`kind`: title, pin, field, out). */
export type FacePart = { readonly text: string } | { readonly ref: string; readonly kind: 'title' | 'pin' | 'field' | 'out' | 'op'; readonly pin?: number; readonly field?: string }

export interface BlockDescriptor {
  readonly id: number
  readonly name: string
  readonly alias: string
  readonly category: string
  readonly description: string
  readonly inputs?: BlockPins
  readonly outputs?: BlockPins
  /** A device block: what it can do with the device, one entry per operation (the pins depend on the one chosen). */
  readonly ops?: readonly BlockOp[]
  readonly op_field?: string
  readonly device_field?: string
  /** The block's headline: the words, then the value part they are set to. */
  readonly face?: { readonly lead: readonly FacePart[]; readonly value?: readonly FacePart[] }
  readonly shape: { readonly min_in: number; readonly max_in: number; readonly min_q: number; readonly max_q: number; readonly min_en: number; readonly required_in: number; readonly state_len: number }
  /** Absent for a block with no state (IF, SWITCH, CLONE). */
  readonly state?: { readonly struct: string; readonly size: number; readonly fields: readonly StateField[] }
  /** The block's own enums (`//#block-enum` in its header): a field's `enum_ref` names one of these or a published one. */
  readonly enums?: Readonly<Record<string, { readonly alias: string; readonly members: readonly BlockEnumMember[] }>>
}

export interface BlockEnumMember {
  readonly name: string
  readonly value: number
  readonly alias: string
  readonly description?: string
}

/** The members of the enum a field of `block` names: the block's own (`//#block-enum`), else the published one; none if neither. */
export const blockEnumMembers = (block: BlockDescriptor | undefined, enumName: string): readonly BlockEnumMember[] => {
  const own = block?.enums?.[enumName]
  if (own) return own.members
  const published = (enumsJson as unknown as { enums: Record<string, { members: readonly BlockEnumMember[] }> }).enums[enumName]
  return published?.members ?? []
}

const files = import.meta.glob('@data-structures/vm/blocks/*.block.json', { eager: true, import: 'default' }) as Record<string, BlockDescriptor>

/** The palette: every block the firmware has, by type id. */
export const BLOCKS: ReadonlyMap<number, BlockDescriptor> = new Map(Object.values(files).map((d) => [d.id, d]))

/** A block by its type id, its `VM_BLK_` name (`VM_BLK_EXPR`), or the short name (`EXPR`). */
export const blockDescriptor = (which: number | string): BlockDescriptor => {
  const found =
    typeof which === 'number'
      ? BLOCKS.get(which)
      : [...BLOCKS.values()].find((d) => d.name === which || d.name === `VM_BLK_${which}`)
  if (!found) throw new RangeError(`No block '${which}'.`)
  return found
}

export type StateValue = number | boolean | string | readonly (number | boolean | string)[]

/** A member by its C name or by its alias (the name the user sees): both are exact, nothing is guessed from a name's ending. */
const enumValue = (enums: EnumMembers, enumName: string, name: string): number => {
  const member = enums[enumName]?.members.find((m) => m.name === name || m.alias === name)
  if (!member) throw new RangeError(`'${name}' is not a member of ${enumName}.`)
  return member.value
}

/**
 * A block's state bytes from field values by name (`{ pt: 500, mode: 'ON_DELAY' }`). A field of an enum takes the member's number or
 * its name; padding and run-time fields are zero. Fields the app does not fill from a name (a `derived` field: a FOR's span; an
 * EXPR's code) are given in `raw`: `{ span: Uint8Array }`. A name that is not a field is refused, so a typo does not become zeros.
 */
export const encodeState = (
  block: BlockDescriptor,
  values: Readonly<Record<string, StateValue | Uint8Array>> = {},
  enums: EnumMembers = (enumsJson as unknown as { enums: EnumMembers }).enums,
): Uint8Array => {
  const state = block.state ?? { size: 0, fields: [] as readonly StateField[] }
  const out = new Uint8Array(state.size)
  const view = new DataView(out.buffer)
  const known = new Set(state.fields.map((f) => f.name))
  for (const name of Object.keys(values)) if (!known.has(name)) throw new RangeError(`${block.name} has no state field '${name}'.`)
  for (const field of state.fields) {
    const given = values[field.name]
    if (given === undefined) continue
    if (field.source === 'padding' || field.source === 'runtime') throw new RangeError(`${block.name}.${field.name} is not set by the app.`)
    if (given instanceof Uint8Array) {
      if (given.length > field.size) throw new RangeError(`${block.name}.${field.name} takes ${field.size} bytes (got ${given.length}).`)
      out.set(given, field.offset)
      continue
    }
    const items = Array.isArray(given) ? given : [given]
    const width = field.element_size ?? field.size
    const count = field.array_len ?? 1
    if (items.length > count) throw new RangeError(`${block.name}.${field.name} takes ${count} value(s) (got ${items.length}).`)
    items.forEach((item, i) => {
      const n = typeof item === 'string' ? enumValue(enums, field.enum_ref ?? '', item) : typeof item === 'boolean' ? Number(item) : item
      const at = field.offset + i * width
      switch (field.c_type) {
        case 'uint8_t': view.setUint8(at, n); break
        case 'uint16_t': view.setUint16(at, n, true); break
        case 'uint32_t': view.setUint32(at, n >>> 0, true); break
        case 'int32_t': view.setInt32(at, n, true); break
        case 'float': view.setFloat32(at, n, true); break
        default: throw new RangeError(`${block.name}.${field.name} (${field.c_type}) cannot be set from a number: pass its bytes.`)
      }
    })
  }
  return out
}
