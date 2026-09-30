/** A BLE UUID. Numeric 16-bit aliases (for example 0xFFE0) are accepted for platform requests. */
export type BleUuid = string | number

export type BleCharacteristicProperty =
  | 'broadcast'
  | 'read'
  | 'write-without-response'
  | 'write'
  | 'notify'
  | 'indicate'
  | 'authenticated-signed-writes'
  | 'reliable-write'
  | 'writable-auxiliaries'

export interface BleDevice {
  readonly id: string
  readonly name?: string
}

export interface BleCharacteristic {
  /** Stable only for the lifetime of the current connection/discovery result. */
  readonly id: string
  readonly uuid: BleUuid
  readonly serviceUuid: BleUuid
  readonly properties: readonly BleCharacteristicProperty[]
  readonly descriptors: readonly BleDescriptor[]
}

export interface BleDescriptor {
  readonly uuid: BleUuid
  readonly name: string
  /** Web Bluetooth reports this descriptor from characteristic capabilities, not a descriptor walk. */
  readonly discovery: 'enumerated' | 'inferred'
}

export interface BleService {
  readonly uuid: BleUuid
  readonly primary: boolean
  readonly characteristics: readonly BleCharacteristic[]
}

export interface BleGattDatabase {
  readonly services: readonly BleService[]
}

export interface BleRequestDeviceOptions {
  /** Required by Web Bluetooth unless acceptAllDevices is set. */
  readonly filters?: readonly BleDeviceFilter[]
  /** Services that must be requested up front for Web Bluetooth access. */
  readonly optionalServiceUuids?: readonly BleUuid[]
  readonly acceptAllDevices?: boolean
}

export interface BleDeviceFilter {
  readonly name?: string
  readonly namePrefix?: string
  readonly serviceUuids?: readonly BleUuid[]
}

export interface BlePairingOptions {
  /** Native adapters may use this when their OS exposes passkey pairing. */
  readonly passkey?: string
}

export interface BleCapability {
  readonly available: boolean
  readonly reason?: string
}

export interface BleAdapterCapabilities {
  readonly interactiveDeviceSelection: BleCapability
  readonly pairing: BleCapability
  readonly passkeyEntry: BleCapability
  readonly rememberedDevices: BleCapability
  readonly forgetDevice: BleCapability
  readonly mtu: BleCapability
  readonly linkInfo: BleCapability
  readonly writeLimit: BleCapability
}

export interface BleMtuInfo {
  /** Null means the host negotiated an MTU but does not expose its value. */
  readonly value: number | null
  readonly source: 'adapter' | 'host-managed' | 'unavailable'
}

export type BleConnectionState = 'idle' | 'connecting' | 'connected' | 'disconnecting' | 'disconnected' | 'failed'
export type BlePairingState = 'unknown' | 'not-paired' | 'pairing' | 'paired'

export interface BleLinkInfo {
  readonly mtu: BleMtuInfo
  readonly rssiDbm: number | null
  readonly txPowerDbm: number | null
  readonly phy: string | null
  readonly connectionIntervalMs: number | null
  readonly slaveLatency: number | null
  readonly supervisionTimeoutMs: number | null
}

export interface BleWriteLimitInfo {
  /** Null means the platform does not expose the ATT payload limit. */
  readonly value: number | null
  readonly source: 'adapter' | 'host-managed' | 'unavailable'
}

export interface BleDiagnostics {
  readonly connectCount: number
  readonly discoveryCount: number
  readonly readCount: number
  readonly writeCount: number
  readonly notificationCount: number
  readonly bytesRead: number
  readonly bytesWritten: number
  readonly lastConnectedAt: number | null
  readonly lastDisconnectedAt: number | null
  readonly lastNotificationAt: number | null
}

export type BleNotificationHandler = (data: Uint8Array, characteristic: BleCharacteristic) => void
export type BleDisconnectHandler = (device: BleDevice) => void
export type BleConnectionStateHandler = (state: BleConnectionState) => void
