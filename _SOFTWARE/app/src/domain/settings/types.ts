/*
 * The project's settings in editor form: what the Settings screens show, saved as is. They follow the firmware's routers: `ble`
 * (GATT services and characteristics created at runtime) and `conn` (outbound data streams and where they are sent). The log and
 * power options are not modelled here: their forms are built from the generated contracts (contractConfig.ts).
 */

/** A GATT characteristic (sys_ble_char_cfg_t): the router's CHAR_CREATE carries the properties, the buffers and the name. */
export interface BleCharacteristicSettings {
  id: string
  name: string
  /** 16-bit hex, e.g. `0xEE01`. */
  uuid: string
  /** The board's own (firmware): refreshed from the board setup on load, never edited. */
  system?: boolean
  write: boolean
  notify: boolean
  indicate: boolean
  /** Frames longer than one packet: every packet starts with a fragment header byte. */
  framed: boolean
  /** Static RAM buffers, bytes; 0 = none. */
  txBufferSize: number
  rxBufferSize: number
}

export interface BleServiceSettings {
  id: string
  name: string
  uuid: string
  system?: boolean
  isPrimary: boolean
  characteristics: BleCharacteristicSettings[]
}

/** Editor only so far: the firmware sets these at build time (Kconfig), the `ble` router has no commands for them. */
export interface BleGeneralSettings {
  deviceName: string
  advIntervalMs: number
  advFastTimeoutSec: number
  connectable: boolean
  txPowerDbm: number
  minConnIntervalMs: number
  maxConnIntervalMs: number
  connLatency: number
  supervisionTimeoutMs: number
  mtuSize: number
  securityMode: 'just_works' | 'passkey' | 'mitm'
  passkeyPin: string
}

export interface BleProfile {
  name: string
  general: BleGeneralSettings
  services: BleServiceSettings[]
}

/** Where a connector sends: one binding per transport (the `conn` router's BIND). Outbound only. */
export interface ConnectorBindingSettings {
  id: string
  provider: 'BLE' | 'UART'
  /** BLE: characteristic UUID (`0xFFE3`); UART: the console endpoint (`0`). */
  endpoint: string
}

export interface ConnectorSettings {
  /** The connector's ID in the registry (system 0..3, user from `SYS_CONNECTOR_APP_BASE`). */
  id: number
  key: string
  /** The name the board keeps (up to 15 characters). */
  name: string
  alias: string
  /** The stream byte every frame of the connector starts with, one byte hex (`0x03`). */
  stream: string
  system: boolean
  description: string
  isSuspended: boolean
  bindings: ConnectorBindingSettings[]
}

export interface ProjectSettings {
  readonly ble: BleProfile
  readonly connectors: readonly ConnectorSettings[]
}

export const DEFAULT_BLE_GENERAL: BleGeneralSettings = {
  deviceName: 'runit',
  advIntervalMs: 100,
  advFastTimeoutSec: 30,
  connectable: true,
  txPowerDbm: 3,
  minConnIntervalMs: 15,
  maxConnIntervalMs: 30,
  connLatency: 0,
  supervisionTimeoutMs: 5000,
  mtuSize: 512,
  securityMode: 'just_works',
  passkeyPin: '123456',
}
