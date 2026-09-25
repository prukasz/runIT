import { describe, expect, it } from 'vitest'
import { packStruct, unpackStruct } from '.'
import type { PacketField } from '.'

describe('unpackStruct', () => {
  it('reads back what packStruct wrote', () => {
    const fields: PacketField[] = [
      { kind: 'scalar', name: 'id', type: 'uint8_t' },
      { kind: 'scalar', name: 'offset', type: 'int16_t' },
      { kind: 'scalar', name: 'gain', type: 'float' },
      { kind: 'scalar', name: 'slots', type: 'uint8_t', length: 3 },
      { kind: 'scalar', name: 'big', type: 'uint64_t' },
      { kind: 'struct', name: 'inner', fields: [{ kind: 'scalar', name: 'flag', type: 'bool' }] },
      { kind: 'text', name: 'name', nulTerminate: true },
      { kind: 'bytes', name: 'rest' },
    ]
    const values = { id: 7, offset: -2, gain: 0.5, slots: [1, 2, 3], big: 2n ** 40n, inner: { flag: true }, name: 'rail', rest: Uint8Array.from([9, 9]) }
    const packed = packStruct(fields, values)
    const { values: read, consumed } = unpackStruct(fields, packed)
    expect(consumed).toBe(packed.byteLength)
    expect(read).toEqual(values)
  })

  it('fails on short data, naming the field', () => {
    expect(() => unpackStruct([{ kind: 'scalar', name: 'duty', type: 'uint32_t' }], Uint8Array.from([1, 2]))).toThrow(/duty/)
  })
})
