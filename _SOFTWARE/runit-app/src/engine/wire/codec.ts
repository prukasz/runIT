import { OBJ_WIDTH } from './types'
import type { ObjType, Scalar, Value } from './types'

const scalar = (item: Scalar): number => (typeof item === 'boolean' ? (item ? 1 : 0) : item)

/** The bytes of a value for an object of type `type`, little-endian. Raw bytes pass through unchanged. */
export const encodeValue = (type: ObjType, value: Value): Uint8Array => {
  if (value instanceof Uint8Array) return value
  const items: readonly Scalar[] = Array.isArray(value) ? (value as readonly Scalar[]) : [value as Scalar]
  const width = OBJ_WIDTH[type].wire
  const out = new Uint8Array(items.length * width)
  const view = new DataView(out.buffer)
  items.forEach((item, i) => {
    const at = i * width
    const n = scalar(item)
    switch (type) {
      case 'u8':
      case 'str':
        view.setUint8(at, n)
        break
      case 'bool':
        view.setUint8(at, n ? 1 : 0)
        break
      case 'u32':
        view.setUint32(at, n >>> 0, true)
        break
      case 'i32':
        view.setInt32(at, n, true)
        break
      case 'f32':
        view.setFloat32(at, n, true)
        break
      case 'ptr':
        view.setUint16(at, n, true)
        break
    }
  })
  return out
}

/** The value in an object's wire bytes: one element as a number (bool as boolean), several as an array; text stays bytes. */
export const decodeValue = (type: ObjType, bytes: Uint8Array): number | boolean | number[] | boolean[] | Uint8Array => {
  if (type === 'str') return bytes
  const width = OBJ_WIDTH[type].wire
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
  const out: Scalar[] = []
  for (let at = 0; at + width <= bytes.length; at += width) {
    switch (type) {
      case 'u8':
        out.push(view.getUint8(at))
        break
      case 'bool':
        out.push(view.getUint8(at) !== 0)
        break
      case 'u32':
        out.push(view.getUint32(at, true))
        break
      case 'i32':
        out.push(view.getInt32(at, true))
        break
      case 'f32':
        out.push(view.getFloat32(at, true))
        break
      case 'ptr':
        out.push(view.getUint16(at, true))
        break
    }
  }
  return (out.length === 1 ? out[0] : out) as number | boolean | number[] | boolean[]
}
