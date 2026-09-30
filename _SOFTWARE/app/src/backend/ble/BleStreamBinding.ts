import type { OutgoingMessage, OutgoingTransport, ReceivedDataStream, RouteBinding } from '../stream'
import type { BleAdapter, BleCharacteristic, BleGattDatabase, BleUuid } from './index'

export interface BleInboundBinding {
  readonly targetId: string
  readonly route: string
  readonly serviceUuid: BleUuid
  readonly characteristicUuid: BleUuid
}

const comparableUuid = (uuid: BleUuid): string => {
  if (typeof uuid === 'number') return `short:${uuid.toString(16).padStart(4, '0').toLowerCase()}`
  const compact = uuid.trim().toLowerCase().replaceAll('-', '').replace(/^0x/, '')
  if (/^[0-9a-f]{4}$/.test(compact)) return `short:${compact}`
  if (/^0000[0-9a-f]{4}00001000800000805f9b34fb$/.test(compact)) return `short:${compact.slice(4, 8)}`
  return `uuid:${compact}`
}

const findCharacteristic = (database: BleGattDatabase, serviceUuid: BleUuid, characteristicUuid: BleUuid): BleCharacteristic | undefined =>
  database.services
    .filter((service) => comparableUuid(service.uuid) === comparableUuid(serviceUuid))
    .flatMap((service) => service.characteristics)
    .find((characteristic) => comparableUuid(characteristic.uuid) === comparableUuid(characteristicUuid))

/** BLE implementation of an outbound transport sender plus explicit inbound subscription binding. */
export class BleStreamBinding implements OutgoingTransport {
  readonly kind = 'ble'
  private readonly adapter: BleAdapter
  private readonly received: ReceivedDataStream

  constructor(adapter: BleAdapter, received: ReceivedDataStream) {
    this.adapter = adapter
    this.received = received
  }

  async validateBinding(binding: RouteBinding): Promise<void> {
    const characteristic = this.resolveOutbound(binding)
    if (!characteristic.properties.includes('write') && !characteristic.properties.includes('write-without-response')) {
      throw new Error(`BLE endpoint '${characteristic.uuid}' is not writable.`)
    }
  }

  async send(binding: RouteBinding, message: OutgoingMessage): Promise<void> {
    const characteristic = this.resolveOutbound(binding)
    const withResponse = binding.endpoint.options?.withResponse !== false
    await this.adapter.write(characteristic, message.data, withResponse)
  }

  async attachInbound(binding: BleInboundBinding): Promise<() => Promise<void>> {
    const database = this.requireDatabase(binding.targetId)
    const characteristic = findCharacteristic(database, binding.serviceUuid, binding.characteristicUuid)
    if (!characteristic) throw new Error('Configured BLE inbound endpoint is absent from the latest GATT discovery.')
    if (!characteristic.properties.includes('notify') && !characteristic.properties.includes('indicate')) {
      throw new Error(`BLE endpoint '${characteristic.uuid}' cannot notify or indicate.`)
    }
    return this.adapter.subscribe(characteristic, (data) => {
      this.received.publish({
        data,
        transport: this.kind,
        targetId: binding.targetId,
        endpoint: `${String(binding.serviceUuid)}/${String(binding.characteristicUuid)}`,
        route: binding.route,
        receivedAt: Date.now(),
      })
    })
  }

  private resolveOutbound(binding: RouteBinding): BleCharacteristic {
    if (binding.endpoint.transport !== this.kind) throw new Error(`Binding '${binding.id}' is not a BLE binding.`)
    const serviceUuid = binding.endpoint.options?.serviceUuid
    if (typeof serviceUuid !== 'string' && typeof serviceUuid !== 'number') {
      throw new Error(`BLE binding '${binding.id}' requires endpoint.options.serviceUuid.`)
    }
    const database = this.requireDatabase(binding.targetId)
    const characteristic = findCharacteristic(database, serviceUuid, binding.endpoint.address)
    if (!characteristic) throw new Error(`BLE endpoint '${binding.endpoint.address}' is absent from the latest GATT discovery.`)
    return characteristic
  }

  private requireDatabase(targetId: string): BleGattDatabase {
    const device = this.adapter.getConnectedDevice()
    if (!device || device.id !== targetId) throw new Error(`BLE target '${targetId}' is not connected through this adapter.`)
    const database = this.adapter.getGattDatabase()
    if (!database) throw new Error('BLE bindings require GATT rediscovery before activation.')
    return database
  }
}
