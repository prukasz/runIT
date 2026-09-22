import type { BleUuid } from './ble'

const RUNIT_GATT_NAMES: Readonly<Record<string, string>> = {
  '0xFFE0': 'runIT service',
  '0xFFE1': 'runIT TX (telemetry and interface notifications)',
  '0xFFE2': 'runIT RX (interface receiver / write)',
  '0xFFE3': 'runIT logs and errors notifications',
  '0xFFE4': 'runIT status notifications',
}

/** Return a standard Bluetooth 16-bit alias from a numeric, short, or base UUID. */
export const shortBleUuid = (uuid: BleUuid): string | undefined => {
  if (typeof uuid === 'number') return `0x${uuid.toString(16).padStart(4, '0').toUpperCase()}`
  const clean = uuid.trim().toLowerCase()
  if (/^[0-9a-f]{4}$/.test(clean)) return `0x${clean.toUpperCase()}`
  if (/^0x[0-9a-f]{4}$/.test(clean)) return `0x${clean.slice(2).toUpperCase()}`
  const baseUuid = /^0000([0-9a-f]{4})-0000-1000-8000-00805f9b34fb$/.exec(clean)
  return baseUuid ? `0x${baseUuid[1].toUpperCase()}` : undefined
}

export const knownRunitGattName = (uuid: BleUuid): string | undefined => {
  const shortUuid = shortBleUuid(uuid)
  return shortUuid ? RUNIT_GATT_NAMES[shortUuid] : undefined
}
