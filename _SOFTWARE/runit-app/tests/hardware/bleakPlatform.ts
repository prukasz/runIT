import type { BleChannel, BleDeviceRef, BlePlatform, LinkDefinitions, OpenHandlers } from '@backend'
import { Helper } from './bridge'

const toHex = (b: Uint8Array) => Buffer.from(b).toString('hex')

/*
 * A BlePlatform on the PC's Bluetooth stack, through bleak (bleak_bridge.py), for Node: what the browser's Web Bluetooth is for
 * the app. It moves packets and nothing else; every byte of framing, every command and the connection logic is the backend's.
 */
export class BleakPlatform implements BlePlatform {
  readonly available = true
  private readonly helper = new Helper('bleak_bridge.py')
  private handlers: OpenHandlers | undefined
  private known: BleDeviceRef | undefined
  /** The MTU of the last connection (what the OS negotiated). */
  mtu = 0

  constructor(
    private readonly defs: LinkDefinitions,
    private readonly options: { name?: string; address?: string } = {},
  ) {
    this.helper.onEvent((m) => {
      if (m.event === 'packet') this.handlers?.onPacket(m.char as number, Uint8Array.from(Buffer.from(m.data as string, 'hex')))
      else if (m.event === 'closed') this.handlers?.onClosed(String(m.reason))
    })
  }

  async requestDevice(): Promise<BleDeviceRef> {
    const found = await this.helper.call('find', { service: this.defs.gatt.service, name: this.options.name ?? 'runit', address: this.options.address ?? null, timeout: 10 })
    this.known = { id: found.address as string, name: found.name as string | undefined }
    return this.known
  }

  async knownDevices(): Promise<readonly BleDeviceRef[]> {
    return this.known ? [this.known] : []
  }

  async forget(): Promise<void> {
    this.known = undefined
  }

  async open(device: BleDeviceRef, handlers: OpenHandlers): Promise<BleChannel> {
    this.handlers = handlers
    const { gatt } = this.defs
    const opened = await this.helper.call('open', { address: device.id, service: gatt.service, write: gatt.rx, notify: [gatt.tx, gatt.logs] })
    this.mtu = opened.mtu as number
    return {
      write: async (packet) => {
        await this.helper.call('write', { data: toHex(packet) })
      },
      close: async () => {
        this.handlers = undefined
        await this.helper.call('close')
      },
    }
  }

  /** Disconnects from the PC's side as if the radio had lost the board: the link is told it was lost. */
  async dropFromHost(): Promise<void> {
    await this.helper.call('drop')
  }

  stop(): void {
    this.helper.stop()
  }
}
