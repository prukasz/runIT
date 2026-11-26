import type { ServiceCatalog } from '../catalog/services'
import { bundledEnums, bundledServiceCatalog } from '../definitions'
import { createDeviceCall } from '../protocol/calls'
import type { BleChannel, BleDeviceRef, BlePlatform, OpenHandlers, OpenSpec } from '../transport/types'
import type { RunitLink } from './link'
import { Services } from './services'

/*
 * The platform of a link to a board behind the one this app is connected to (the master): a tunnel (DATA_MATRIX.MD) through
 * the master's bridge characteristic (RUNIT.MD "The bridge") onto a UART of the master's that goes to the other board's. A
 * link on it is a session of its own with that board, end to end: the master passes the frames whole and reads none of them,
 * and the app's session with the master goes on beside it.
 *
 *   const remote = new RunitLink(new BridgePlatform(master, () => wire))
 *   await remote.connect(REMOTE_BOARD, { retry: true })
 *
 * Opening makes the UART on the master anew (a device under `BRIDGE_DEVICE`; the firmware makes a framed UART's provider by
 * itself, PROVIDER_UART.MD) and links it with the bridge both ways. It is made at run time: a master that restarts loses it, and
 * the link's next attempt makes it again. A master that drops closes the tunnel, so this link drops with it and comes back with it.
 */

/** The master's device ID of the bridge's UART: the last one, which no project device is given before every other is taken. */
export const BRIDGE_DEVICE = 63

/** A UART device's provider: this plus the device's ID (PROVIDER_UART.MD "Made with the device"). */
const UART_PROVIDER_BASE = 0x40
/** The provider id of the master's bridge/slave UART (`UART_PROVIDER_BASE + BRIDGE_DEVICE`). */
export const BRIDGE_UART_PROVIDER = UART_PROVIDER_BASE + BRIDGE_DEVICE
/** The master's provider of the bridge characteristic (`RUNIT_PROVIDER_BLE_BRIDGE`, runit_ids.h). */
export const BRIDGE_PROVIDER = 0x06

const UART_TYPE = 'esp32s3_uart'

/** The master's UART to the other board: its controller, rate, and the master's own GPIO pins. */
export interface BridgeWire {
  readonly uart: number
  /** The master's TX pin: it goes to the other board's RX. */
  readonly tx: number
  /** The master's RX pin: it comes from the other board's TX. */
  readonly rx: number
  /** The other board's console rate (`CONFIG_ESP_CONSOLE_UART_BAUDRATE`). */
  readonly baud: number
}

/** The wire of the runIT PCB to a devkit's RX and TX pins: UART1, GPIO 17 and 18, the console's rate. */
export const DEFAULT_WIRE: BridgeWire = { uart: 1, tx: 17, rx: 18, baud: 115200 }

/** The one reference of the board behind the master: which board it is, its hello says. */
export const REMOTE_BOARD: BleDeviceRef = { id: 'bridge', name: 'Remote board' }

const enumValue = (group: string, symbol: string): number => {
  const member = bundledEnums()[group]?.members.find((m) => m.name === symbol)
  if (!member) throw new Error(`enums.json is missing ${group}.${symbol}`)
  return member.value
}

export class BridgePlatform implements BlePlatform {
  readonly available = true
  private readonly services: Services
  private packets = 0

  constructor(
    private readonly master: RunitLink,
    private readonly wire: () => BridgeWire,
    private readonly catalog: ServiceCatalog = bundledServiceCatalog(),
  ) {
    this.services = new Services(master, catalog)
  }

  /** Packets that came from the other board since the last open: some with no session says the board is heard and does not hear. */
  get heard(): number {
    return this.packets
  }

  requestDevice(): Promise<BleDeviceRef> {
    return Promise.resolve(REMOTE_BOARD)
  }

  knownDevices(): Promise<readonly BleDeviceRef[]> {
    return Promise.resolve([REMOTE_BOARD])
  }

  forget(): Promise<void> {
    return Promise.resolve()
  }

  async open(_device: BleDeviceRef, spec: OpenSpec, handlers: OpenHandlers): Promise<BleChannel> {
    this.packets = 0
    if (this.master.state !== 'connected') throw new Error('The main board is not connected.')
    await this.makeWire()
    // The other board's frames come on the bridge as the session's do on its own characteristic: the link takes them as those.
    return this.master.tunnel(this.master.defs.gatt.bridge, {
      onPacket: (_characteristic, packet) => {
        this.packets++
        handlers.onPacket(spec.write, packet)
      },
      onClosed: (reason) => handlers.onClosed(reason),
    })
  }

  /** Creates the master's framed UART device for the wire. */
  private async createUart(): Promise<void> {
    const wire = this.wire()
    const create = createDeviceCall(this.catalog, BRIDGE_DEVICE, UART_TYPE, {
      uart_num: wire.uart,
      mode: enumValue('sys_uart_mode_e', 'SYS_UART_MODE_FRAMED'),
      baud: wire.baud,
      parity: enumValue('sys_uart_parity_e', 'SYS_UART_PARITY_NONE'),
      stop_bits: 1,
      tx_pin: { device: 0, pin: wire.tx },
      rx_pin: { device: 0, pin: wire.rx },
      rts_pin: null,
      cts_pin: null,
    } as never)
    await this.services.send(create)
  }

  /** Detaches any master session on the bridge UART provider (no-op when none is attached). */
  async detachSession(): Promise<void> {
    await this.services.call('SESSION', 'DETACH', { provider: BRIDGE_UART_PROVIDER }, { side: 'write', timeoutMs: 1000, label: 'bridge SESSION DETACH' }).catch(() => undefined)
  }

  /** The UART on the master and its two links with the bridge, made anew (app ↔ slave tunnel). */
  private async makeWire(): Promise<void> {
    await this.release()
    await this.createUart()
    const port = (provider: number) => (this.master.defs.providerPortKind << 8) | provider
    const bridge = port(BRIDGE_PROVIDER)
    const uart = port(BRIDGE_UART_PROVIDER)
    for (const [from, to] of [[bridge, uart], [uart, bridge]] as const) {
      await this.services.call('DATA_MATRIX', 'CONNECT', { from, to }, { side: 'write' })
    }
  }

  /** Hands the UART to the master board itself as a slave-board session (`SYS_SESSION_SLAVE`) so the master's VM can call the slave directly. */
  async attachSlave(): Promise<void> {
    if (this.master.state !== 'connected') throw new Error('The main board is not connected.')
    await this.release()
    await this.createUart()
    await this.services.call('SESSION', 'ATTACH', {
      provider: BRIDGE_UART_PROVIDER,
      flags: enumValue('session_flag', 'SYS_SESSION_SLAVE'),
      deadman_ms: 0,
      modules: Uint8Array.from([]),
    }, { side: 'write', label: 'slave SESSION ATTACH' })
  }

  /** Takes the bridge's session and UART off the master (its provider and links); nothing when the master has none. */
  async release(): Promise<void> {
    await this.detachSession()
    const lifecycle = this.catalog.byContractId.get(this.catalog.lifecycleContractId!)!
    const { value } = await this.services.device(this.catalog.allDevices!, lifecycle.id, 'LIST', undefined, { side: 'read', label: 'devices LIST (bridge)' })
    const there = (value as readonly Record<string, unknown>[]).find((row) => Number(row.id) === BRIDGE_DEVICE)
    if (!there) return
    if (Number(there.type_id) !== this.catalog.device(UART_TYPE).typeId) throw new Error(`The main board's device ${BRIDGE_DEVICE} is not a UART: the bridge needs that ID.`)
    await this.services.device(BRIDGE_DEVICE, lifecycle.id, 'REMOVE', undefined, { side: 'write', label: 'bridge UART REMOVE' })
  }
}
