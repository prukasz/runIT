import type {
  BleAdapterCapabilities,
  BleCharacteristic,
  BleConnectionState,
  BleConnectionStateHandler,
  BleDiagnostics,
  BleDevice,
  BleDisconnectHandler,
  BleGattDatabase,
  BleLinkInfo,
  BleMtuInfo,
  BleNotificationHandler,
  BlePairingState,
  BlePairingOptions,
  BleRequestDeviceOptions,
  BleWriteLimitInfo,
} from './types'

/**
 * Transport-only BLE contract.  The protocol layer owns runIT UUID selection,
 * packet framing, command acknowledgement, and retries.
 */
export interface BleAdapter {
  readonly kind: string

  isAvailable(): boolean
  getCapabilities(): BleAdapterCapabilities
  requestDevice(options: BleRequestDeviceOptions): Promise<BleDevice>
  getRememberedDevices(): Promise<readonly BleDevice[]>
  connect(device: BleDevice): Promise<void>
  disconnect(): Promise<void>
  getConnectionState(): BleConnectionState
  onStateChange(handler: BleConnectionStateHandler): () => void
  isConnected(): boolean
  getConnectedDevice(): BleDevice | undefined
  /** Latest discovery snapshot; undefined until discover() succeeds or after disconnect. */
  getGattDatabase(): BleGattDatabase | undefined
  pair(options?: BlePairingOptions): Promise<void>
  getPairingState(): Promise<BlePairingState>
  forgetDevice(device: BleDevice): Promise<void>
  discover(): Promise<BleGattDatabase>
  getMtu(): Promise<BleMtuInfo>
  getLinkInfo(): Promise<BleLinkInfo>
  getWriteLimit(): Promise<BleWriteLimitInfo>
  getDiagnostics(): BleDiagnostics
  read(characteristic: BleCharacteristic): Promise<Uint8Array>
  write(characteristic: BleCharacteristic, data: Uint8Array, withResponse?: boolean): Promise<void>
  subscribe(characteristic: BleCharacteristic, handler: BleNotificationHandler): Promise<() => Promise<void>>
  onDisconnect(handler: BleDisconnectHandler): () => void
}
