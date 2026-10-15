import type { LinkDefinitions } from '../definitions'
import type { BleChannel, BleDeviceRef, BlePlatform, OpenHandlers } from './types'

/*
 * The platform on Web Bluetooth (Chromium, a secure context). Opening is the shortest path to a working link: connect, ask for
 * the runIT service alone (not a walk over every service), take its characteristics in one call and subscribe only to the two
 * the link listens to. The browser runs one GATT operation at a time and rejects overlapping ones, so every operation goes
 * through one chain. Web Bluetooth shows neither the MTU nor the connection parameters, so nothing here depends on them: a
 * write is acknowledged (flow control, nothing is dropped) and the platform splits a long one itself.
 */

/** The part of the Web Bluetooth API used here (the standard DOM library does not ship it). */
interface Properties {
  readonly write: boolean
  readonly writeWithoutResponse: boolean
  readonly notify: boolean
  readonly indicate: boolean
}
type Listener = (event: { readonly target: unknown }) => void
interface Emitter {
  addEventListener(type: string, listener: Listener): void
  removeEventListener(type: string, listener: Listener): void
}
interface Characteristic extends Emitter {
  readonly uuid: string
  readonly properties: Properties
  readonly value?: DataView
  writeValueWithResponse(value: Uint8Array): Promise<void>
  writeValueWithoutResponse(value: Uint8Array): Promise<void>
  startNotifications(): Promise<unknown>
}
interface Service {
  getCharacteristics(): Promise<Characteristic[]>
}
interface GattServer {
  readonly connected: boolean
  connect(): Promise<GattServer>
  disconnect(): void
  getPrimaryService(uuid: number): Promise<Service>
}
interface Device extends Emitter {
  readonly id: string
  readonly name?: string
  readonly gatt?: GattServer
}
export interface WebBluetooth {
  requestDevice(options: { filters: { services: number[] }[]; optionalServices: number[] }): Promise<Device>
  getDevices?(): Promise<Device[]>
}

const BASE_UUID = /^0000([0-9a-f]{4})-0000-1000-8000-00805f9b34fb$/i

/** The 16-bit alias of a UUID the browser reports (`0000ffe1-0000-1000-8000-00805f9b34fb` is 0xFFE1); NaN for any other. */
export const shortUuid = (uuid: string): number => {
  const match = BASE_UUID.exec(uuid)
  return match ? Number.parseInt(match[1]!, 16) : Number.NaN
}

const browserBluetooth = (): WebBluetooth | undefined => (globalThis as { navigator?: { bluetooth?: WebBluetooth } }).navigator?.bluetooth

export class WebBluetoothPlatform implements BlePlatform {
  private readonly devices = new Map<string, Device>()

  constructor(
    private readonly defs: LinkDefinitions,
    private readonly bluetooth: WebBluetooth | undefined = browserBluetooth(),
  ) {}

  get available(): boolean {
    return this.bluetooth !== undefined
  }

  async requestDevice(): Promise<BleDeviceRef> {
    const device = await this.require().requestDevice({ filters: [{ services: [this.defs.gatt.service] }], optionalServices: [this.defs.gatt.service] })
    this.devices.set(device.id, device)
    return { id: device.id, name: device.name }
  }

  async knownDevices(): Promise<readonly BleDeviceRef[]> {
    const bluetooth = this.require()
    if (typeof bluetooth.getDevices !== 'function') return []
    const devices = await bluetooth.getDevices()
    for (const device of devices) this.devices.set(device.id, device)
    return devices.map((device) => ({ id: device.id, name: device.name }))
  }

  async open(ref: BleDeviceRef, handlers: OpenHandlers): Promise<BleChannel> {
    const device = this.devices.get(ref.id)
    const gatt = device?.gatt
    if (!device || !gatt) throw new Error('Choose the board first (requestDevice), or take it from knownDevices.')

    let ours = false // we are dropping the link: not reported as a loss
    let lost = false
    const gone = () => {
      if (ours || lost) return
      lost = true
      cleanup()
      handlers.onClosed('The board disconnected.')
    }
    const listeners = new Map<Characteristic, Listener>()
    const cleanup = () => {
      device.removeEventListener('gattserverdisconnected', gone)
      for (const [characteristic, listener] of listeners) characteristic.removeEventListener('characteristicvaluechanged', listener)
      listeners.clear()
    }
    device.addEventListener('gattserverdisconnected', gone)

    // one GATT operation at a time; a failed one does not stop the ones after it
    let chain: Promise<unknown> = Promise.resolve()
    const gattOp = <T>(operation: () => Promise<T>): Promise<T> => {
      const result = chain.then(operation)
      chain = result.catch(() => undefined)
      return result
    }

    let rx: Characteristic
    try {
      const server = gatt.connected ? gatt : await gatt.connect()
      const service = await server.getPrimaryService(this.defs.gatt.service)
      const characteristics = await service.getCharacteristics()
      const found = characteristics.find((c) => shortUuid(c.uuid) === this.defs.gatt.rx)
      if (!found) throw new Error('The board has no runIT request characteristic.')
      rx = found
      for (const uuid of [this.defs.gatt.tx, this.defs.gatt.logs]) {
        const characteristic = characteristics.find((c) => shortUuid(c.uuid) === uuid)
        if (!characteristic) continue
        const listener: Listener = (event) => {
          const value = (event.target as Characteristic).value
          if (value) handlers.onPacket(uuid, new Uint8Array(value.buffer.slice(value.byteOffset, value.byteOffset + value.byteLength)))
        }
        characteristic.addEventListener('characteristicvaluechanged', listener)
        listeners.set(characteristic, listener)
        await gattOp(() => characteristic.startNotifications())
      }
    } catch (error) {
      ours = true
      cleanup()
      if (gatt.connected) gatt.disconnect()
      throw error
    }

    const response = rx.properties.write || !rx.properties.writeWithoutResponse
    return {
      write: (packet) => gattOp(() => (response ? rx.writeValueWithResponse(packet) : rx.writeValueWithoutResponse(packet))),
      close: async () => {
        ours = true
        cleanup()
        if (gatt.connected) gatt.disconnect()
      },
    }
  }

  private require(): WebBluetooth {
    if (!this.bluetooth) throw new Error('Web Bluetooth is unavailable. Use a Chromium browser in a secure context.')
    return this.bluetooth
  }
}
