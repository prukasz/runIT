import { describe, expect, it } from 'vitest'
import type { WireField } from '@backend'
import { hexText, showValue } from '../../src/app/devices/devices'
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

describe('a number the descriptor shows in hex', () => {
  const field = { id: 'addr', label: '7-bit address', type: 'integer', rawType: 'u8', offset: 0, optional: false, pinRef: false, display: 'hex', max: 127 } satisfies WireField
  const none = () => undefined
  it('is written 0x46, with the digits its largest value needs', () => {
    expect(hexText(0x46, 127)).toBe('0x46')
    expect(hexText(5, 127)).toBe('0x05')
    expect(hexText(0x1ff, 0xffff)).toBe('0x01FF')
    expect(showValue(field, 0x46, none, () => '')).toBe('0x46')
    expect(showValue({ ...field, display: undefined } as WireField, 70, none, () => '')).toBe('70')
  })
})
