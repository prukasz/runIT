import type { BleAdapter } from '../BleAdapter'
import { BleAdapterError } from '../errors'
import type {
  BleAdapterCapabilities,
  BleCharacteristic,
  BleCharacteristicProperty,
  BleConnectionState,
  BleConnectionStateHandler,
  BleDiagnostics,
  BleDevice,
  BleDeviceFilter,
  BleDisconnectHandler,
  BleGattDatabase,
  BleLinkInfo,
  BleMtuInfo,
  BleNotificationHandler,
  BlePairingOptions,
  BleRequestDeviceOptions,
  BleService,
  BleWriteLimitInfo,
} from '../types'
import type {
  WebBluetooth,
  WebBluetoothCharacteristic,
  WebBluetoothCharacteristicProperties,
  WebBluetoothDevice,
  WebBluetoothGattServer,
  WebBluetoothRequestOptions,
  WebBluetoothServiceUuid,
} from './webBluetoothPlatform'

type BrowserCharacteristic = WebBluetoothCharacteristic

const CCCD_UUID = '00002902-0000-1000-8000-00805f9b34fb'

const browserBluetooth = (): WebBluetooth | undefined => (globalThis.navigator as Navigator & { bluetooth?: WebBluetooth }).bluetooth

const toDevice = (device: WebBluetoothDevice): BleDevice => ({ id: device.id, name: device.name })

const toProperties = (properties: WebBluetoothCharacteristicProperties): BleCharacteristicProperty[] => {
  const result: BleCharacteristicProperty[] = []
  if (properties.broadcast) result.push('broadcast')
  if (properties.read) result.push('read')
  if (properties.writeWithoutResponse) result.push('write-without-response')
  if (properties.write) result.push('write')
  if (properties.notify) result.push('notify')
  if (properties.indicate) result.push('indicate')
  if (properties.authenticatedSignedWrites) result.push('authenticated-signed-writes')
  if (properties.reliableWrite) result.push('reliable-write')
  if (properties.writableAuxiliaries) result.push('writable-auxiliaries')
  return result
}

const toCharacteristic = (characteristic: BrowserCharacteristic): BleCharacteristic => {
  const properties = toProperties(characteristic.properties)
  const hasCccd = properties.includes('notify') || properties.includes('indicate')
  return {
    id: `${characteristic.service.uuid}/${characteristic.uuid}`,
    uuid: characteristic.uuid,
    serviceUuid: characteristic.service.uuid,
    properties,
    descriptors: hasCccd ? [{ uuid: CCCD_UUID, name: 'Client Characteristic Configuration', discovery: 'inferred' }] : [],
  }
}

const toData = (value: DataView): Uint8Array => new Uint8Array(value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength))

/** Desktop Web Bluetooth adapter. Requires a secure context and a user gesture for requestDevice(). */
export class WebBluetoothAdapter implements BleAdapter {
  readonly kind = 'web-bluetooth'

  private device?: WebBluetoothDevice
  private database?: BleGattDatabase
  private readonly knownDevices = new Map<string, WebBluetoothDevice>()
  private readonly characteristics = new Map<string, BrowserCharacteristic>()
  private readonly disconnectHandlers = new Set<BleDisconnectHandler>()
  private readonly stateHandlers = new Set<BleConnectionStateHandler>()
  private readonly notificationHandlers = new Map<string, EventListener>()
  private state: BleConnectionState = 'idle'
  private diagnostics: BleDiagnostics = {
    connectCount: 0,
    discoveryCount: 0,
    readCount: 0,
    writeCount: 0,
    notificationCount: 0,
    bytesRead: 0,
    bytesWritten: 0,
    lastConnectedAt: null,
    lastDisconnectedAt: null,
    lastNotificationAt: null,
  }

  isAvailable(): boolean {
    return typeof navigator !== 'undefined' && browserBluetooth() !== undefined
  }

  getCapabilities(): BleAdapterCapabilities {
    const supported = this.isAvailable()
    const unavailable = { available: false, reason: 'Web Bluetooth is unavailable in this browser or context.' }
    return {
      interactiveDeviceSelection: supported ? { available: true } : unavailable,
      pairing: supported ? { available: true, reason: 'The operating system manages pairing when a secured device requires it.' } : unavailable,
      passkeyEntry: { available: false, reason: 'Web Bluetooth does not expose passkey entry; the operating system owns that prompt.' },
      rememberedDevices: supported && typeof browserBluetooth()?.getDevices === 'function'
        ? { available: true }
        : { available: false, reason: 'This browser does not expose previously granted devices.' },
      forgetDevice: { available: false, reason: 'Web Bluetooth cannot revoke browser permission or remove an operating-system bond.' },
      mtu: { available: false, reason: 'Web Bluetooth negotiates MTU through the host but does not expose its value or control.' },
      linkInfo: { available: false, reason: 'Web Bluetooth does not expose RSSI, PHY, connection parameters, or TX power.' },
      writeLimit: { available: false, reason: 'Web Bluetooth does not expose the negotiated ATT write limit.' },
    }
  }

  async requestDevice(options: BleRequestDeviceOptions): Promise<BleDevice> {
    const bluetooth = this.requireBluetooth()
    const requestOptions = this.toRequestOptions(options)
    const device = await bluetooth.requestDevice(requestOptions)
    this.rememberDevice(device)
    return toDevice(device)
  }

  async getRememberedDevices(): Promise<readonly BleDevice[]> {
    const bluetooth = this.requireBluetooth()
    if (typeof bluetooth.getDevices !== 'function') {
      throw new BleAdapterError('This browser cannot list remembered Bluetooth devices.', 'unsupported')
    }
    return (await bluetooth.getDevices()).map((device) => {
      this.knownDevices.set(device.id, device)
      return toDevice(device)
    })
  }

  async connect(device: BleDevice): Promise<void> {
    const browserDevice = this.requireKnownDevice(device)
    this.rememberDevice(browserDevice)
    if (!browserDevice.gatt) throw new BleAdapterError('Selected device does not expose GATT.', 'invalid-device')
    this.setState('connecting')
    try {
      if (!browserDevice.gatt.connected) await browserDevice.gatt.connect()
      this.database = undefined
      this.diagnostics = { ...this.diagnostics, connectCount: this.diagnostics.connectCount + 1, lastConnectedAt: Date.now() }
      this.setState('connected')
    } catch (error) {
      this.setState('failed')
      throw error
    }
  }

  async disconnect(): Promise<void> {
    this.setState('disconnecting')
    if (this.device?.gatt?.connected) this.device.gatt.disconnect()
    this.database = undefined
    this.characteristics.clear()
    this.notificationHandlers.clear()
    this.setState('disconnected')
  }

  getConnectionState(): BleConnectionState {
    return this.state
  }

  onStateChange(handler: BleConnectionStateHandler): () => void {
    this.stateHandlers.add(handler)
    return () => this.stateHandlers.delete(handler)
  }

  isConnected(): boolean {
    return this.device?.gatt?.connected === true
  }

  getConnectedDevice(): BleDevice | undefined {
    return this.isConnected() && this.device ? toDevice(this.device) : undefined
  }

  getGattDatabase(): BleGattDatabase | undefined {
    return this.database
  }

  async pair(_options?: BlePairingOptions): Promise<void> {
    if (!this.isConnected()) throw new BleAdapterError('Connect before pairing.', 'not-connected')
    // Browser/OS pairing is initiated automatically by an operation that requires encryption.
  }

  async getPairingState(): Promise<'unknown'> {
    return 'unknown'
  }

  async forgetDevice(_device: BleDevice): Promise<void> {
    throw new BleAdapterError('Web Bluetooth cannot forget a device or revoke its operating-system bond.', 'unsupported')
  }

  async discover(): Promise<BleGattDatabase> {
    const server = this.requireServer()
    this.characteristics.clear()
    const services: BleService[] = []
    for (const service of await server.getPrimaryServices()) {
      const characteristics = (await service.getCharacteristics()).map((characteristic: BrowserCharacteristic) => {
        const browserCharacteristic = characteristic
        const normalized = toCharacteristic(browserCharacteristic)
        this.characteristics.set(normalized.id, browserCharacteristic)
        return normalized
      })
      services.push({ uuid: service.uuid, primary: true, characteristics })
    }
    this.database = { services }
    this.diagnostics = { ...this.diagnostics, discoveryCount: this.diagnostics.discoveryCount + 1 }
    return this.database
  }

  async getMtu(): Promise<BleMtuInfo> {
    return (await this.getLinkInfo()).mtu
  }

  async getLinkInfo(): Promise<BleLinkInfo> {
    this.requireServer()
    return {
      mtu: { value: null, source: 'host-managed' },
      rssiDbm: null,
      txPowerDbm: null,
      phy: null,
      connectionIntervalMs: null,
      slaveLatency: null,
      supervisionTimeoutMs: null,
    }
  }

  async getWriteLimit(): Promise<BleWriteLimitInfo> {
    this.requireServer()
    return { value: null, source: 'host-managed' }
  }

  getDiagnostics(): BleDiagnostics {
    return { ...this.diagnostics }
  }

  async read(characteristic: BleCharacteristic): Promise<Uint8Array> {
    const browserCharacteristic = this.resolveCharacteristic(characteristic)
    const data = toData(await browserCharacteristic.readValue())
    this.diagnostics = { ...this.diagnostics, readCount: this.diagnostics.readCount + 1, bytesRead: this.diagnostics.bytesRead + data.byteLength }
    return data
  }

  async write(characteristic: BleCharacteristic, data: Uint8Array, withResponse = true): Promise<void> {
    const browserCharacteristic = this.resolveCharacteristic(characteristic)
    if (withResponse) {
      await browserCharacteristic.writeValueWithResponse(data)
    } else {
      await browserCharacteristic.writeValueWithoutResponse(data)
    }
    this.diagnostics = { ...this.diagnostics, writeCount: this.diagnostics.writeCount + 1, bytesWritten: this.diagnostics.bytesWritten + data.byteLength }
  }

  async subscribe(characteristic: BleCharacteristic, handler: BleNotificationHandler): Promise<() => Promise<void>> {
    const browserCharacteristic = this.resolveCharacteristic(characteristic)
    const key = characteristic.id
    if (this.notificationHandlers.has(key)) throw new BleAdapterError(`Already subscribed to ${characteristic.uuid}.`, 'operation-failed')
    const listener: EventListener = (event) => {
      const value = (event.target as BrowserCharacteristic).value
      if (value) {
        const data = toData(value)
        this.diagnostics = {
          ...this.diagnostics,
          notificationCount: this.diagnostics.notificationCount + 1,
          bytesRead: this.diagnostics.bytesRead + data.byteLength,
          lastNotificationAt: Date.now(),
        }
        handler(data, characteristic)
      }
    }
    await browserCharacteristic.startNotifications()
    browserCharacteristic.addEventListener('characteristicvaluechanged', listener)
    this.notificationHandlers.set(key, listener)

    return async () => {
      const activeListener = this.notificationHandlers.get(key)
      if (!activeListener) return
      browserCharacteristic.removeEventListener('characteristicvaluechanged', activeListener)
      this.notificationHandlers.delete(key)
      if (this.isConnected()) await browserCharacteristic.stopNotifications()
    }
  }

  onDisconnect(handler: BleDisconnectHandler): () => void {
    this.disconnectHandlers.add(handler)
    return () => this.disconnectHandlers.delete(handler)
  }

  private requireBluetooth(): WebBluetooth {
    const bluetooth = this.isAvailable() ? browserBluetooth() : undefined
    if (!bluetooth) throw new BleAdapterError('Web Bluetooth is unavailable. Use a Chromium browser in a secure context.', 'unavailable')
    return bluetooth
  }

  private requireKnownDevice(device: BleDevice): WebBluetoothDevice {
    const browserDevice = this.knownDevices.get(device.id)
    if (!browserDevice) {
      throw new BleAdapterError('Connect requires a device returned by this adapter. Request or restore it first.', 'invalid-device')
    }
    return browserDevice
  }

  private requireServer(): WebBluetoothGattServer {
    if (!this.device?.gatt?.connected) throw new BleAdapterError('No connected GATT server.', 'not-connected')
    return this.device.gatt
  }

  private resolveCharacteristic(characteristic: BleCharacteristic): BrowserCharacteristic {
    this.requireServer()
    const result = this.characteristics.get(characteristic.id)
    if (!result) throw new BleAdapterError('Characteristic is absent from the latest discovery result.', 'invalid-device')
    return result
  }

  private rememberDevice(device: WebBluetoothDevice): void {
    if (this.device && this.device !== device) this.device.removeEventListener('gattserverdisconnected', this.handleDisconnect)
    this.knownDevices.set(device.id, device)
    this.device = device
    this.device.addEventListener('gattserverdisconnected', this.handleDisconnect)
  }

  private readonly handleDisconnect = (): void => {
    const disconnected = this.device
    this.database = undefined
    this.characteristics.clear()
    this.notificationHandlers.clear()
    this.diagnostics = { ...this.diagnostics, lastDisconnectedAt: Date.now() }
    this.setState('disconnected')
    if (disconnected) {
      const device = toDevice(disconnected)
      this.disconnectHandlers.forEach((handler) => handler(device))
    }
  }

  private setState(state: BleConnectionState): void {
    if (this.state === state) return
    this.state = state
    this.stateHandlers.forEach((handler) => handler(state))
  }

  private toRequestOptions(options: BleRequestDeviceOptions): WebBluetoothRequestOptions {
    if (options.acceptAllDevices) {
      return { acceptAllDevices: true, optionalServices: options.optionalServiceUuids }
    }
    if (!options.filters?.length) {
      throw new BleAdapterError('Provide at least one filter or set acceptAllDevices.', 'invalid-device')
    }
    return {
      filters: options.filters.map((filter) => this.toFilter(filter)),
      optionalServices: options.optionalServiceUuids,
    }
  }

  private toFilter(filter: BleDeviceFilter): { name?: string; namePrefix?: string; services?: readonly WebBluetoothServiceUuid[] } {
    return { name: filter.name, namePrefix: filter.namePrefix, services: filter.serviceUuids }
  }
}
