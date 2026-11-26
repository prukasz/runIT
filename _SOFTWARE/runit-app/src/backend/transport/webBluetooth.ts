import type { LinkDefinitions } from '../definitions'
import type { BleChannel, BleDeviceRef, BlePlatform, OpenHandlers, OpenSpec } from './types'

/*
 * The platform on Web Bluetooth (Chromium, a secure context). Opening is the shortest path to a working link: connect, ask for
 * the one service (no walk over every service), take its characteristics in one call, subscribe to the ones the link hears. The
 * browser runs one GATT operation at a time and rejects overlapping ones, so every operation goes through one chain. Web
 * Bluetooth shows neither the MTU nor the connection parameters: nothing here depends on them.
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
  /** Not in every browser: makes the browser scan for this device, which fires `advertisementreceived`. */
  watchAdvertisements?(options: { signal: AbortSignal }): Promise<void>
  /** Not in every browser: revokes the page's permission for this device. */
  forget?(): Promise<void>
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

/** How long a board known from an earlier visit is waited for to advertise before the connection is given up. */
const ADVERTISEMENT_WAIT_MS = 10_000

const text = (error: unknown): string => (error instanceof Error ? error.message : String(error))
const hex = (uuid: number): string => `0x${uuid.toString(16).toUpperCase()}`

/*
 * A board the browser knows from an earlier visit (`getDevices()`, after a reload) is connectable only once the browser has seen it
 * advertise: before that `connect()` fails with "Bluetooth Device is no longer in range". The first failure is answered by listening
 * for one advertisement (`watchAdvertisements`, where the browser has it) and connecting again.
 */
const connectSeen = async (device: Device, gatt: GattServer): Promise<GattServer> => {
  try {
    return await gatt.connect()
  } catch (error) {
    if (typeof device.watchAdvertisements !== 'function') {
      throw new Error(`${text(error)}. This browser cannot listen for the board's advertisement (watchAdvertisements, "Experimental Web Platform features"): choose it in the chooser.`)
    }
    const watch = new AbortController()
    let onAdvertisement: Listener = () => undefined
    let timer: ReturnType<typeof setTimeout> | undefined
    const seen = new Promise<void>((resolve, reject) => {
      onAdvertisement = () => resolve()
      timer = setTimeout(() => reject(error), ADVERTISEMENT_WAIT_MS)
    })
    device.addEventListener('advertisementreceived', onAdvertisement)
    try {
      await device.watchAdvertisements({ signal: watch.signal }).catch(() => undefined)
      await seen
    } finally {
      clearTimeout(timer)
      device.removeEventListener('advertisementreceived', onAdvertisement)
      watch.abort()
    }
    return gatt.connect()
  }
}

/** Connects (when not connected) and takes the characteristics of `service`; a failure leaves the board disconnected. */
const discover = async (device: Device, service: number, connect: (gatt: GattServer) => Promise<GattServer>): Promise<Characteristic[]> => {
  const gatt = device.gatt!
  try {
    const server = gatt.connected ? gatt : await connect(gatt)
    return await (await server.getPrimaryService(service)).getCharacteristics()
  } catch (error) {
    if (gatt.connected) gatt.disconnect()
    throw error
  }
}

const browserBluetooth =(): WebBluetooth | undefined => (globalThis as { navigator?: { bluetooth?: WebBluetooth } }).navigator?.bluetooth

export interface WebBluetoothOptions {
  /**
   * Finds the board anew by a scan (the desktop app: `requestDevice()` answered by the board's address) and gives its new
   * reference, or undefined when the scan did not see it. Used when the page holds no device for the reference (the app just
   * started) and when connecting to the one it holds fails (Windows keeps a stale device after the board restarts). Without it
   * a held device that does not connect is waited for to advertise (`watchAdvertisements`).
   */
  readonly find?: (ref: BleDeviceRef) => Promise<BleDeviceRef | undefined>
}

export class WebBluetoothPlatform implements BlePlatform {
  private readonly devices = new Map<string, Device>()
  private readonly find?: (ref: BleDeviceRef) => Promise<BleDeviceRef | undefined>

  constructor(
    private readonly defs: LinkDefinitions,
    private readonly bluetooth: WebBluetooth | undefined = browserBluetooth(),
    options: WebBluetoothOptions = {},
  ) {
    this.find = options.find
  }

  get available(): boolean {
    return this.bluetooth !== undefined
  }

  /** The browser lists the boards it allowed before (`getDevices()`), so a reloaded page opens one with no chooser. */
  get remembersBoards(): boolean {
    return typeof this.bluetooth?.getDevices === 'function'
  }

  async requestDevice(): Promise<BleDeviceRef> {
    const service = this.defs.gatt.service
    const device = await this.require().requestDevice({ filters: [{ services: [service] }], optionalServices: [service] })
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

  async forget(ref: BleDeviceRef): Promise<void> {
    const device = this.devices.get(ref.id)
    this.devices.delete(ref.id)
    device?.gatt?.disconnect()
    await device?.forget?.()
  }

  async open(ref: BleDeviceRef, spec: OpenSpec, handlers: OpenHandlers): Promise<BleChannel> {
    let device = this.devices.get(ref.id)
    if (!device?.gatt && !this.find) throw new Error('Choose the board first (requestDevice), or take it from knownDevices.')

    // Connect and read the service. After a board restart on Windows, Chromium's cached device often stays stale: its connect()
    // fails, or it succeeds and the link drops at once ("GATT Server is disconnected"). With `find`, that (or no device held) is
    // answered by a fresh scan for the board and one more try; without it, by waiting for the board to advertise.
    let characteristics: Characteristic[] | undefined
    let failure: unknown
    if (device?.gatt) {
      try {
        const held = device
        characteristics = await discover(held, spec.service, (gatt) => (this.find ? gatt.connect() : connectSeen(held, gatt)))
      } catch (error) {
        if (!this.find) throw error
        failure = error
      }
    }
    if (!characteristics) {
      const fresh = await this.find!(ref)
      const next = fresh ? this.devices.get(fresh.id) : undefined
      if (!next?.gatt) throw failure ?? new Error(`${ref.name ?? 'The board'} was not found nearby.`)
      this.devices.set(ref.id, next)
      device = next
      characteristics = await discover(device, spec.service, (gatt) => gatt.connect())
    }
    const opened = device!
    const gatt = opened.gatt!

    let ours = false // we drop the link: not reported as a loss
    let lost = false
    const listeners = new Map<Characteristic, Listener>()
    const cleanup = () => {
      opened.removeEventListener('gattserverdisconnected', gone)
      for (const [characteristic, listener] of listeners) characteristic.removeEventListener('characteristicvaluechanged', listener)
      listeners.clear()
    }
    const gone: Listener = () => {
      if (ours || lost) return
      lost = true
      cleanup()
      handlers.onClosed('The board disconnected.')
    }
    opened.addEventListener('gattserverdisconnected', gone)

    // one GATT operation at a time; a failed one does not stop the ones after it
    let chain: Promise<unknown> = Promise.resolve()
    const gattOp = <T>(operation: () => Promise<T>): Promise<T> => {
      const result = chain.then(operation)
      chain = result.catch(() => undefined)
      return result
    }

    let written: Characteristic
    try {
      if (!gatt.connected) throw new Error('The board disconnected while the link opened.')
      const find = (uuid: number) => characteristics.find((c) => shortUuid(c.uuid) === uuid)
      const target = find(spec.write)
      if (!target) throw new Error(`The board's service ${hex(spec.service)} has no characteristic ${hex(spec.write)} to write.`)
      written = target
      const heard = [...spec.notify.map((uuid) => ({ uuid, required: true })), ...(spec.optional ?? []).map((uuid) => ({ uuid, required: false }))]
      for (const { uuid, required } of heard) {
        const characteristic = find(uuid)
        if (!characteristic) {
          if (required) throw new Error(`The board's service ${hex(spec.service)} has no characteristic ${hex(uuid)}.`)
          continue
        }
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

    // Without a response when the characteristic takes it: about ten times faster. The link's window is the flow control.
    const response = !written.properties.writeWithoutResponse
    return {
      write: (packet) => gattOp(() => (response ? written.writeValueWithResponse(packet) : written.writeValueWithoutResponse(packet))),
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
