import { describe, expect, it } from 'vitest'
import { addressText, scanAddresses, scanBitmap } from '../../src/app/devices/i2cScan'

describe('the I2C bus scan answer', () => {
  // 4 × u32 little-endian: address a is bit (a & 31) of found[a >> 5], that is bit (a & 7) of byte (a >> 3).
  it('reads the addresses a bitmap says answered, lowest first', () => {
    const found = Uint8Array.of(0x00, 0x00, 0x01, 0x40, 0x08, 0x00, 0x00, 0x00, 0x81, 0x01, 0x14, 0x00, 0x01, 0x00, 0x01, 0x00)
    expect(scanAddresses(found).map(addressText)).toEqual(['0x10', '0x1E', '0x23', '0x40', '0x47', '0x48', '0x52', '0x54', '0x60', '0x70'])
    expect(scanAddresses(new Uint8Array(16))).toEqual([])
  })

  it('takes the bitmap out of the answer struct, and only a bitmap', () => {
    const found = Uint8Array.of(1, 0)
    expect(scanBitmap({ found })).toBe(found)
    expect(scanBitmap({ found: [1, 0] })).toEqual([1, 0])
    expect(scanBitmap({ other: 1 })).toBeUndefined()
    expect(scanBitmap(undefined)).toBeUndefined()
  })
})
