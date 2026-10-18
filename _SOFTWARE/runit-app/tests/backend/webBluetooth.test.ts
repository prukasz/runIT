import { describe, expect, it } from 'vitest'
import { WebBluetoothPlatform } from '@backend'
import type { WebBluetooth } from '@backend/transport/webBluetooth'
import { defs } from './fakes'

/* A browser that knows the board from an earlier visit: `connect()` fails until it has seen the board advertise. */

type Handler = (event: { target: unknown }) => void

const hex = (n: number) => `0000${n.toString(16).padStart(4, '0')}-0000-1000-8000-00805f9b34fb`

const browser = (options: { watch: boolean }) => {
  const handlers = new Map<string, Set<Handler>>()
  const log: string[] = []
  let seen = false
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
    getPrimaryService: async () => ({ getCharacteristics: async () => [characteristic(defs.gatt.rx, false), characteristic(defs.gatt.tx, true), characteristic(defs.gatt.logs, true)] }),
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
              for (const h of handlers.get('advertisementreceived') ?? []) h({ target: device })
            }, 5)
          },
        }
      : {}),
  }
  const bluetooth: WebBluetooth = { requestDevice: async () => device, getDevices: async () => [device] }
  return { bluetooth, log, handlers }
}

describe('opening a board the browser knows from an earlier visit', () => {
  it('listens for one advertisement after the first failed connect, then connects, and stops listening', async () => {
    const { bluetooth, log, handlers } = browser({ watch: true })
    const platform = new WebBluetoothPlatform(defs, bluetooth)
    const [known] = await platform.knownDevices()
    const channel = await platform.open(known!, { onPacket() {}, onClosed() {} })
    expect(log).toEqual(['connect', 'watch', 'connect'])
    expect(handlers.get('advertisementreceived')?.size ?? 0).toBe(0)
    await channel.close()
  })

  it('a browser without watchAdvertisements gives the first error back', async () => {
    const { bluetooth, log } = browser({ watch: false })
    const platform = new WebBluetoothPlatform(defs, bluetooth)
    const [known] = await platform.knownDevices()
    await expect(platform.open(known!, { onPacket() {}, onClosed() {} })).rejects.toThrow(/no longer in range/)
    expect(log).toEqual(['connect'])
  })
})

describe('forgetting a board', () => {
  it('disconnects it and revokes the permission in the browser', async () => {
    const { bluetooth, log } = browser({ watch: false })
    const platform = new WebBluetoothPlatform(defs, bluetooth)
    const [known] = await platform.knownDevices()
    await platform.forget(known!)
    expect(log).toEqual(['forget'])
    await expect(platform.open(known!, { onPacket() {}, onClosed() {} })).rejects.toThrow(/Choose the board first/)
  })
})
