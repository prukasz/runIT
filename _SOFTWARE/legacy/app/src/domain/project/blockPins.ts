import type { ProgramBlock } from './document'

export const isDynamicInput = (block: ProgramBlock, index: number): boolean => !!block.inputs?.[index] || !!block.dynamicInputs?.includes(index)

/** JSON keeps wide masks as hexadecimal strings, so pins above 52 stay exact. */
export const readPinMask = (raw: number | string | undefined): bigint | undefined => {
  if (typeof raw === 'number') return Number.isSafeInteger(raw) && raw >= 0 ? BigInt(raw) : undefined
  if (typeof raw === 'string' && /^(?:0x[0-9a-f]+|\d+)$/i.test(raw)) return BigInt(raw)
  return raw === undefined ? 0n : undefined
}

export const pinMaskText = (mask: bigint): string => `0x${mask.toString(16)}`
