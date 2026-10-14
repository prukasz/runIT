import type { Field, RawType, FieldScalar } from '../catalog/types'

/*
 * A field's value as the bytes of the block's state: the descriptor says where (`offset`), how wide and of what kind
 * (`rawType`), and by how much the shown value is scaled to the wire value (`scale`). Everything is little-endian
 * (`__packed` structs on a little-endian board).
 */

export interface RawRange {
  readonly width: number
  readonly min: number
  readonly max: number
}

/** The bytes and the representable range of each raw type. `float` has no integer range. */
export const RAW: Readonly<Record<Exclude<RawType, 'bytes'>, RawRange>> = {
  u8: { width: 1, min: 0, max: 0xff },
  i8: { width: 1, min: -0x80, max: 0x7f },
  u16: { width: 2, min: 0, max: 0xffff },
  i16: { width: 2, min: -0x8000, max: 0x7fff },
  u32: { width: 4, min: 0, max: 0xffffffff },
  i32: { width: 4, min: -0x80000000, max: 0x7fffffff },
  float: { width: 4, min: -Infinity, max: Infinity },
}

export type ValueResult = { readonly ok: true; readonly number: number } | { readonly ok: false; readonly message: string }

const fail = (message: string): ValueResult => ({ ok: false, message })

/**
 * The number a field's value stands for on the wire: the value (or the field's `fixed`, else its default) as a number, enum
 * and boolean values by what they mean, "any" as the wire value the descriptor gives it, times `scale`, rounded for an
 * integer raw type. Checks the choice, the descriptor's range and the raw type's range.
 */
export const wireNumber = (field: Field, given: FieldScalar | undefined): ValueResult => {
  const value = field.fixed ?? given ?? field.default
  if (value === undefined) return fail(`'${field.alias}' has no value.`)

  if (value === 'any' && field.any !== undefined) return { ok: true, number: field.any }

  let shown: number
  if (typeof value === 'boolean') shown = value ? 1 : 0
  else if (typeof value === 'number') shown = value
  else {
    // A string stands for an enum option by its alias or its value as text.
    const option = field.options?.find((o) => String(o.value) === value || o.alias === value)
    if (!option) return fail(`'${value}' is not a value of '${field.alias}'.`)
    shown = typeof option.value === 'number' ? option.value : option.value === true ? 1 : 0
  }
  if (!Number.isFinite(shown)) return fail(`'${field.alias}' is not a finite number.`)

  if (field.type === 'enum' && field.options && !field.options.some((o) => o.value === shown || String(o.value) === String(shown))) {
    return fail(`${shown} is not one of the choices of '${field.alias}'.`)
  }
  if (field.type === 'number') {
    if (field.min !== undefined && shown < field.min) return fail(`'${field.alias}' is ${shown}, below its least ${field.min}${field.unit ? ` ${field.unit}` : ''}.`)
    if (field.max !== undefined && shown > field.max) return fail(`'${field.alias}' is ${shown}, above its most ${field.max}${field.unit ? ` ${field.unit}` : ''}.`)
  }

  const scaled = field.scale !== undefined && field.scale !== 0 ? shown * field.scale : shown
  const raw = field.rawType
  if (raw && raw !== 'float' && raw !== 'bytes') {
    const rounded = Math.round(scaled)
    const range = RAW[raw]
    if (rounded < range.min || rounded > range.max) return fail(`'${field.alias}' is ${shown}, which does not fit ${raw} (${range.min}..${range.max}).`)
    return { ok: true, number: rounded }
  }
  return { ok: true, number: scaled }
}

/** Writes a number into `bytes` at `offset` as `raw`, little-endian. */
export const writeRaw = (bytes: Uint8Array, offset: number, raw: Exclude<RawType, 'bytes'>, value: number): void => {
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  switch (raw) {
    case 'u8':
      view.setUint8(offset, value)
      break
    case 'i8':
      view.setInt8(offset, value)
      break
    case 'u16':
      view.setUint16(offset, value, true)
      break
    case 'i16':
      view.setInt16(offset, value, true)
      break
    case 'u32':
      view.setUint32(offset, value >>> 0, true)
      break
    case 'i32':
      view.setInt32(offset, value, true)
      break
    case 'float':
      view.setFloat32(offset, value, true)
      break
  }
}

/** The 32 bits a float has: how a device block keeps a constant of a scaled argument (`k[]`: "the bits of a float"). */
export const floatBits = (value: number): number => {
  const view = new DataView(new ArrayBuffer(4))
  view.setFloat32(0, value, true)
  return view.getInt32(0, true)
}
