import type { BleUuid } from './types'

/** Return a standard Bluetooth 16-bit alias (`0xFFE1`) from a numeric, short, or base UUID. */
export const shortBleUuid = (uuid: BleUuid): string | undefined => {
  if (typeof uuid === 'number') return `0x${uuid.toString(16).padStart(4, '0').toUpperCase()}`
  const clean = uuid.trim().toLowerCase()
  if (/^[0-9a-f]{4}$/.test(clean)) return `0x${clean.toUpperCase()}`
  if (/^0x[0-9a-f]{4}$/.test(clean)) return `0x${clean.slice(2).toUpperCase()}`
  const baseUuid = /^0000([0-9a-f]{4})-0000-1000-8000-00805f9b34fb$/.exec(clean)
  return baseUuid ? `0x${baseUuid[1].toUpperCase()}` : undefined
}
