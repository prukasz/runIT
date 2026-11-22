/*
 * The I2C contract's SCAN answer (contract_i2c.json): 16 bytes, 4 × u32 little-endian; address `a` sets bit `a & 31` of
 * `found[a >> 5]`, which in bytes is bit `a & 7` of byte `a >> 3`.
 */

/** The contract and command the view reads this way. */
export const I2C_SCAN = { contract: 'I2C', command: 'SCAN', field: 'found' } as const

/** The addresses a SCAN's bitmap says answered, lowest first. */
export const scanAddresses = (found: ArrayLike<number>): number[] => {
  const out: number[] = []
  for (let address = 0; address < found.length * 8; address++) {
    if ((found[address >> 3]! & (1 << (address & 7))) !== 0) out.push(address)
  }
  return out
}

/** An address as 7-bit hex (`0x40`). */
export const addressText = (address: number): string => `0x${address.toString(16).toUpperCase().padStart(2, '0')}`

/** The bitmap of a SCAN answer, when the answer is one. */
export const scanBitmap = (value: unknown): ArrayLike<number> | undefined => {
  const found = (value as Record<string, unknown> | undefined)?.[I2C_SCAN.field]
  return found instanceof Uint8Array || Array.isArray(found) ? (found as ArrayLike<number>) : undefined
}
