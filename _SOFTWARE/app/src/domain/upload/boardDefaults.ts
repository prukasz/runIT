import { runitEnumValue, runitStreamCatalog } from '../descriptors'
import type { StreamCatalog } from '../descriptors'
import type { BleCharSpec, BleServiceSpec, ConnectorBindingSpec, ConnectorSpec, SettingsState } from './settingsUpload'

/*
 * What a board holds after a restart with no stored code: its runIT GATT
 * service and system connectors with their default bindings, from
 * streams.generated.json (runit_board_cfg.c, sdkconfig). The one source for
 * the settings editors' system entries, the Apply planner's baseline and the
 * stored code builder / decoder, so their IDs agree.
 */

const hex = (value: number, digits: number): string => `0x${value.toString(16).padStart(digits, '0').toUpperCase()}`

/** `SYS_UART_ENDPOINT_CONSOLE`, 0 → `Console (0)` (the planner reads the number in parentheses). */
export const uartEndpointText = (endpoint: number, symbol?: string): string => {
  const label = symbol?.replace(/^SYS_UART_ENDPOINT_/, '').toLowerCase().replace(/^./, (c) => c.toUpperCase())
  return label ? `${label} (${endpoint})` : String(endpoint)
}

export const boardDefaultSettings = (streams: StreamCatalog = runitStreamCatalog()): SettingsState => {
  const { board } = streams
  const ble = runitEnumValue('runit_data_provider_e', 'RUNIT_DATA_PROVIDER_BLE')
  const uart = runitEnumValue('runit_data_provider_e', 'RUNIT_DATA_PROVIDER_UART')

  const characteristics: BleCharSpec[] = board.characteristics.map((char) => ({
    id: `sys-${char.symbol.toLowerCase()}`,
    name: char.name,
    uuid: hex(char.uuid, 4),
    system: true,
    // The board sets no READ flag, and is_write allows both write kinds (sys_ble_stack.c).
    read: false,
    write: char.write,
    writeNoResponse: char.write,
    notify: char.notify,
    indicate: char.indicate,
    txBufferSize: char.txBufferSize,
    rxBufferSize: char.rxBufferSize,
  }))
  const service: BleServiceSpec = {
    id: `sys-${board.service.symbol.toLowerCase()}`,
    name: 'runIT service',
    uuid: hex(board.service.uuid, 4),
    system: true,
    isPrimary: board.service.isPrimary,
    characteristics,
  }

  const connectors: ConnectorSpec[] = streams.streams.map((stream) => {
    // One row per provider + endpoint; TX and RX on the same endpoint become TX_RX.
    const rows = new Map<string, ConnectorBindingSpec>()
    for (const binding of board.bindings.filter((entry) => entry.connectorId === stream.connectorId)) {
      const provider = binding.providerId === ble ? 'BLE' : binding.providerId === uart ? 'UART' : undefined
      if (!provider) continue
      const key = `${provider}:${binding.endpoint}`
      const direction = binding.direction === 'tx' ? 'TX' : 'RX'
      const existing = rows.get(key)
      rows.set(key, {
        id: `b_${stream.name}_${provider.toLowerCase()}_${binding.endpoint}`,
        provider,
        endpoint: provider === 'BLE' ? hex(binding.endpoint, 4) : uartEndpointText(binding.endpoint, binding.endpointSymbol),
        direction: existing && existing.direction !== direction ? 'TX_RX' : direction,
      })
    }
    return {
      id: stream.connectorId,
      key: stream.name,
      name: stream.name,
      header: hex(stream.header, 2),
      system: true,
      maxPacketLen: stream.maxFrame,
      isSuspended: false,
      bindings: [...rows.values()],
    }
  })

  return { services: [service], connectors }
}
