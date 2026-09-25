import { BleStreamBinding } from './ble'
import type { BleAdapter } from './ble'
import { CommandClient, RUNIT_ROUTE } from './protocol'
import type { InterfaceProtocol } from './protocol'
import { OutgoingRouter, ReceivedDataStream } from './stream'

/**
 * Where the board's streams live on BLE (16-bit UUIDs). Built from the
 * generated stream catalog (domain/descriptors → runitStreamCatalog().ble),
 * which reads the board's own connector bindings.
 */
export interface RunitBleLayout {
  readonly service: number
  /** Commands are written here. */
  readonly commandWrite: number
  /** Every characteristic the board sends frames on. */
  readonly notify: readonly number[]
}

export interface RunitBleSessionOptions {
  readonly layout: RunitBleLayout
  readonly protocol: InterfaceProtocol
  /** Default wait for a command's answer. */
  readonly timeoutMs?: number
}

/** A connected board's command channel over BLE. */
export interface RunitBleSession {
  readonly targetId: string
  /** Every frame from the board (responses, telemetry, logs, errors), stream byte first. */
  readonly received: ReceivedDataStream
  readonly commands: CommandClient
  readonly layout: RunitBleLayout
  /** Stop listening and cancel pending commands. The BLE link stays up. */
  close(): Promise<void>
}

const hex16 = (uuid: number): string => `0x${uuid.toString(16).padStart(4, '0').toUpperCase()}`

/**
 * Bind the runIT characteristics of an adapter that is connected and has run
 * discover(): commands go out on the layout's write characteristic, every
 * notify characteristic comes in. Closes itself when the board disconnects.
 */
export const openRunitBleSession = async (adapter: BleAdapter, options: RunitBleSessionOptions): Promise<RunitBleSession> => {
  const { layout, protocol } = options
  const device = adapter.getConnectedDevice()
  if (!device) throw new Error('Connect to a runIT board first.')
  if (!adapter.getGattDatabase()) throw new Error('Discover the GATT services first.')

  const targetId = device.id
  const received = new ReceivedDataStream()
  const router = new OutgoingRouter()
  const binding = new BleStreamBinding(adapter, received)
  router.registerTransport(binding)
  await router.replaceBindings([{
    id: 'runit.interface.ble',
    targetId,
    route: RUNIT_ROUTE.interface,
    direction: 'outbound',
    enabled: true,
    priority: 0,
    policy: 'single',
    endpoint: { transport: binding.kind, address: hex16(layout.commandWrite), options: { serviceUuid: layout.service, withResponse: true } },
    generation: 1,
  }])

  const unsubscribes: (() => Promise<void>)[] = []
  const detach = async (): Promise<void> => {
    const pending = unsubscribes.splice(0)
    await Promise.allSettled(pending.map((unsubscribe) => unsubscribe()))
  }
  try {
    for (const characteristicUuid of layout.notify) {
      const route = `${RUNIT_ROUTE.notifyPrefix}${hex16(characteristicUuid)}`
      unsubscribes.push(await binding.attachInbound({ targetId, route, serviceUuid: layout.service, characteristicUuid }))
    }
  } catch (error) {
    await detach()
    throw error
  }

  const commands = new CommandClient({ sender: router, frames: received, protocol, targetId, timeoutMs: options.timeoutMs })
  let closed = false
  const close = async (reason?: string): Promise<void> => {
    if (closed) return
    closed = true
    stopDisconnect()
    commands.close(reason)
    if (adapter.isConnected()) await detach()
    else unsubscribes.length = 0
  }
  const stopDisconnect = adapter.onDisconnect(() => void close('The board disconnected.'))

  return { targetId, received, commands, layout, close: () => close() }
}
