import { describe, expect, it } from 'vitest'
import { WebBluetoothPlatform } from '@backend'
import type { WebBluetooth } from '@backend/transport/webBluetooth'
import { defs } from './fakes'

/* A browser that knows the board from an earlier visit: `connect()` fails until it has seen the board advertise. */

type Handler = (event: { target: unknown }) => void

const hex = (n: number) => `0000${n.toString(16).padStart(4, '0')}-0000-1000-8000-00805f9b34fb`

/** `stale`: the first link after connect drops at once (Windows, a board that restarted), as the service is read. */
const browser = (options: { watch: boolean; stale?: boolean }) => {
  const handlers = new Map<string, Set<Handler>>()
  const log: string[] = []
  let seen = false
  let stale = options.stale ?? false
  const fire = (type: string) => {
    for (const h of [...(handlers.get(type) ?? [])]) h({ target: device })
  }
  const characteristic = (n: number, notify: boolean) => ({
    uuid: hex(n),
    properties: { write: !notify, writeWithoutResponse: false, notify, indicate: false },
    addEventListener() {},
    removeEventListener() {},
    writeValueWithResponse: async () => {},
    writeValueWithoutResponse: async () => {},
    startNotifications: async () => {},
  })
  const server = {
    connected: false,
    async connect() {
      log.push('connect')
      if (!seen) throw new Error('Bluetooth Device is no longer in range.')
      this.connected = true
      return this
    },
    disconnect() {
      this.connected = false
    },
    async getPrimaryService() {
      if (stale) {
        stale = false
        this.connected = false
        fire('gattserverdisconnected')
        throw new Error('GATT Server is disconnected. Cannot retrieve services.')
      }
      return { getCharacteristics: async () => [characteristic(defs.gatt.session, true), characteristic(defs.gatt.logs, true)] }
    },
  }
  const device = {
    id: 'board',
    name: 'runit',
    gatt: server,
    addEventListener(type: string, listener: Handler) {
      handlers.set(type, (handlers.get(type) ?? new Set()).add(listener))
    },
    removeEventListener(type: string, listener: Handler) {
      handlers.get(type)?.delete(listener)
    },
    async forget() {
      log.push('forget')
    },
    ...(options.watch
      ? {
          async watchAdvertisements() {
            log.push('watch')
            setTimeout(() => {
              seen = true
              fire('advertisementreceived')
            }, 5)
          },
        }
      : {}),
  }
  const bluetooth: WebBluetooth = { requestDevice: async () => device, getDevices: async () => [device] }
  return { bluetooth, log, handlers }
}

/** What the link opens: the browser's board has no telemetry characteristic, which is optional. */
const spec = { service: defs.gatt.service, write: defs.gatt.session, notify: [defs.gatt.session], optional: [defs.gatt.logs, defs.gatt.telemetry] }

describe('opening the service', () => {
  it('a characteristic the link must hear and the board lacks fails the open; an optional one is left out', async () => {
    const { bluetooth } = browser({ watch: true })
    const platform = new WebBluetoothPlatform(defs, bluetooth)
    const [known] = await platform.knownDevices()
    await expect(platform.open(known!, { ...spec, notify: [defs.gatt.telemetry] }, { onPacket() {}, onClosed() {} })).rejects.toThrow(/no characteristic/)
    const channel = await platform.open(known!, spec, { onPacket() {}, onClosed() {} })
    await channel.close()
  })
})

describe('opening a board the browser knows from an earlier visit', () => {
  it('listens for one advertisement after the first failed connect, then connects, and stops listening', async () => {
    const { bluetooth, log, handlers } = browser({ watch: true })
    const platform = new WebBluetoothPlatform(defs, bluetooth)
    const [known] = await platform.knownDevices()
    const channel = await platform.open(known!, spec, { onPacket() {}, onClosed() {} })
    expect(log).toEqual(['connect', 'watch', 'connect'])
    expect(handlers.get('advertisementreceived')?.size ?? 0).toBe(0)
    await channel.close()
  })

  it('a browser without watchAdvertisements gives the first error back', async () => {
    const { bluetooth, log } = browser({ watch: false })
    const platform = new WebBluetoothPlatform(defs, bluetooth)
    const [known] = await platform.knownDevices()
    await expect(platform.open(known!, spec, { onPacket() {}, onClosed() {} })).rejects.toThrow(/no longer in range/)
    expect(log).toEqual(['connect'])
  })
})

describe('opening a board that restarted (a stale device on Windows)', () => {
  it('a link that drops as the service is read is opened again after a fresh scan, and its later loss is reported', async () => {
    const { bluetooth, log, handlers } = browser({ watch: true, stale: true })
    let platform!: WebBluetoothPlatform
    const refresh = async () => {
      log.push('refresh')
      return platform.requestDevice()
    }
    platform = new WebBluetoothPlatform(defs, bluetooth, { refresh })
    const [known] = await platform.knownDevices()
    const closed: string[] = []
    const channel = await platform.open(known!, spec, { onPacket() {}, onClosed: (reason) => closed.push(reason) })
    expect(log).toEqual(['connect', 'watch', 'connect', 'refresh', 'connect'])
    expect(closed).toEqual([])
    for (const h of [...(handlers.get('gattserverdisconnected') ?? [])]) h({ target: undefined })
    expect(closed).toEqual(['The board disconnected.'])
    await channel.close()
  })
})

describe('forgetting a board', () => {
  it('disconnects it and revokes the permission in the browser', async () => {
    const { bluetooth, log } = browser({ watch: false })
    const platform = new WebBluetoothPlatform(defs, bluetooth)
    const [known] = await platform.knownDevices()
    await platform.forget(known!)
    expect(log).toEqual(['forget'])
    await expect(platform.open(known!, spec, { onPacket() {}, onClosed() {} })).rejects.toThrow(/Choose the board first/)
  })
})
