/**
 * The part of the Web Bluetooth API the app uses. TypeScript's DOM library does not ship it, and
 * `watchAdvertisements` / `getDevices` are not available in every Chromium.
 */
export interface WbCharacteristic extends EventTarget {
  readonly uuid: string
  readonly value?: DataView
  writeValueWithResponse(value: Uint8Array): Promise<void>
  writeValueWithoutResponse(value: Uint8Array): Promise<void>
  startNotifications(): Promise<WbCharacteristic>
  stopNotifications(): Promise<WbCharacteristic>
}

export interface WbService {
  readonly uuid: string
  getCharacteristic(uuid: number | string): Promise<WbCharacteristic>
}

export interface WbServer {
  readonly connected: boolean
  connect(): Promise<WbServer>
  disconnect(): void
  getPrimaryService(uuid: number | string): Promise<WbService>
}

export interface WbDevice extends EventTarget {
  readonly id: string
  readonly name?: string
  readonly gatt?: WbServer
  readonly watchingAdvertisements?: boolean
  watchAdvertisements?(options?: { signal?: AbortSignal }): Promise<void>
}

export interface WbRequestOptions {
  readonly filters?: readonly { name?: string; namePrefix?: string; services?: readonly (number | string)[] }[]
  readonly optionalServices?: readonly (number | string)[]
  readonly acceptAllDevices?: boolean
}

export interface WbBluetooth {
  requestDevice(options: WbRequestOptions): Promise<WbDevice>
  getDevices?(): Promise<WbDevice[]>
}

export const webBluetooth = (): WbBluetooth | undefined =>
  typeof navigator === 'undefined' ? undefined : (navigator as Navigator & { bluetooth?: WbBluetooth }).bluetooth

export const toBytes = (value: DataView): Uint8Array => new Uint8Array(value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength))
