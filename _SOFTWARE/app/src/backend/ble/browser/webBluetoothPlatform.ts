/**
 * Private structural declarations for the part of the Web Bluetooth API used
 * by this adapter. TypeScript's standard DOM library does not ship them.
 */
export type WebBluetoothServiceUuid = string | number

export interface WebBluetoothCharacteristicProperties {
  readonly broadcast: boolean
  readonly read: boolean
  readonly writeWithoutResponse: boolean
  readonly write: boolean
  readonly notify: boolean
  readonly indicate: boolean
  readonly authenticatedSignedWrites: boolean
  readonly reliableWrite: boolean
  readonly writableAuxiliaries: boolean
}

export interface WebBluetoothCharacteristic extends EventTarget {
  readonly uuid: string
  readonly service: WebBluetoothService
  readonly properties: WebBluetoothCharacteristicProperties
  readonly value?: DataView
  readValue(): Promise<DataView>
  writeValueWithResponse(value: Uint8Array): Promise<void>
  writeValueWithoutResponse(value: Uint8Array): Promise<void>
  startNotifications(): Promise<WebBluetoothCharacteristic>
  stopNotifications(): Promise<WebBluetoothCharacteristic>
}

export interface WebBluetoothService {
  readonly uuid: string
  getCharacteristics(): Promise<WebBluetoothCharacteristic[]>
}

export interface WebBluetoothGattServer {
  readonly connected: boolean
  connect(): Promise<WebBluetoothGattServer>
  disconnect(): void
  getPrimaryServices(): Promise<WebBluetoothService[]>
}

export interface WebBluetoothDevice extends EventTarget {
  readonly id: string
  readonly name?: string
  readonly gatt?: WebBluetoothGattServer
}

export interface WebBluetoothRequestOptions {
  readonly filters?: readonly { name?: string; namePrefix?: string; services?: readonly WebBluetoothServiceUuid[] }[]
  readonly optionalServices?: readonly WebBluetoothServiceUuid[]
  readonly acceptAllDevices?: boolean
}

export interface WebBluetooth {
  requestDevice(options: WebBluetoothRequestOptions): Promise<WebBluetoothDevice>
  getDevices?(): Promise<WebBluetoothDevice[]>
}
