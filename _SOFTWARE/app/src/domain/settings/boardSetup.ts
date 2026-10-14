import streamsJson from '@data-structures/streams/streams.generated.json'

/*
 * What the board holds after a restart, read from data-structures/streams/streams.generated.json (generated from the board's C:
 * the GATT table of sys_ble_link_init(), the sys_connector_bind() calls of runit_board_cfg.c, the published connector / stream
 * byte / transport enums and the registries' Kconfig limits; see streams-annotations.md). One source for the settings editors'
 * system entries.
 */

interface StreamsFile {
  readonly ble: {
    readonly service: { readonly symbol: string; readonly uuid: string; readonly alias: string; readonly primary: boolean; readonly locked: boolean }
    readonly characteristics: readonly {
      readonly symbol: string
      readonly uuid: string
      readonly alias: string
      readonly write: boolean
      readonly notify: boolean
      readonly indicate: boolean
      readonly framed: boolean
      readonly tx_buffer_size: number
      readonly rx_buffer_size: number
    }[]
  }
  readonly uart: { readonly symbol: string; readonly endpoint: number; readonly alias: string }
  readonly transports: readonly { readonly symbol: string; readonly value: number; readonly alias: string }[]
  readonly connectors: readonly { readonly symbol: string; readonly name: string; readonly id: number; readonly alias: string; readonly description: string; readonly stream: number }[]
  readonly bindings: readonly { readonly connector: string; readonly transport: string; readonly endpoint: number; readonly endpoint_symbol: string }[]
  readonly limits: { readonly connectors_max: number; readonly bindings_per_connector: number; readonly name_max: number; readonly frame_max: number; readonly app_base: number }
}

const file = streamsJson as unknown as StreamsFile

export type Provider = 'BLE' | 'UART'

const PROVIDER_OF: Readonly<Record<string, Provider>> = { TRANSPORT_BLE: 'BLE', TRANSPORT_UART: 'UART' }

export const hex = (value: number, digits: number): string => `0x${value.toString(16).padStart(digits, '0').toUpperCase()}`

export interface BoardCharacteristic {
  readonly symbol: string
  readonly uuid: number
  readonly name: string
  readonly write: boolean
  readonly notify: boolean
  readonly indicate: boolean
  readonly framed: boolean
  readonly txBufferSize: number
  readonly rxBufferSize: number
}

/** One system connector: its registry ID and stream byte, and how the firmware names it. */
export interface BoardStream {
  readonly id: number
  /** `logs`, `errors`, `telemetry`, `events`. */
  readonly key: string
  readonly symbol: string
  readonly alias: string
  readonly description: string
  readonly stream: number
}

export interface BoardBinding {
  readonly connectorId: number
  readonly provider: Provider
  /** BLE: characteristic UUID text (`0xFFE3`); UART: the endpoint number as text. */
  readonly endpoint: string
}

export interface BoardSetup {
  readonly service: { readonly uuid: number; readonly symbol: string; readonly name: string; readonly isPrimary: boolean }
  readonly characteristics: readonly BoardCharacteristic[]
  readonly streams: readonly BoardStream[]
  readonly bindings: readonly BoardBinding[]
  readonly uart: { readonly endpoint: number; readonly name: string }
  readonly limits: { readonly connectorsMax: number; readonly bindingsPerConnector: number; readonly nameMax: number; readonly frameMax: number }
  /** First ID a user connector may take. */
  readonly appBase: number
}

const parseUuid = (text: string): number => Number.parseInt(text, 16)

let cached: BoardSetup | undefined

export const boardSetup = (): BoardSetup => {
  if (cached) return cached
  const streams: BoardStream[] = file.connectors.map((connector) => ({
    id: connector.id,
    key: connector.name,
    symbol: connector.symbol,
    alias: connector.alias,
    description: connector.description,
    stream: connector.stream,
  }))
  const bindings = file.bindings.map((binding): BoardBinding => {
    const stream = streams.find((entry) => entry.symbol === binding.connector)
    const provider = PROVIDER_OF[binding.transport]
    if (!stream || !provider) throw new Error(`streams.generated.json: binding of ${binding.connector} over ${binding.transport} names no known connector or transport.`)
    return { connectorId: stream.id, provider, endpoint: provider === 'BLE' ? hex(binding.endpoint, 4) : String(binding.endpoint) }
  })
  cached = {
    service: { uuid: parseUuid(file.ble.service.uuid), symbol: file.ble.service.symbol, name: file.ble.service.alias, isPrimary: file.ble.service.primary },
    characteristics: file.ble.characteristics.map((char) => ({
      symbol: char.symbol,
      uuid: parseUuid(char.uuid),
      name: char.alias,
      write: char.write,
      notify: char.notify,
      indicate: char.indicate,
      framed: char.framed,
      txBufferSize: char.tx_buffer_size,
      rxBufferSize: char.rx_buffer_size,
    })),
    streams,
    bindings,
    uart: { endpoint: file.uart.endpoint, name: file.uart.alias },
    limits: { connectorsMax: file.limits.connectors_max, bindingsPerConnector: file.limits.bindings_per_connector, nameMax: file.limits.name_max, frameMax: file.limits.frame_max },
    appBase: file.limits.app_base,
  }
  return cached
}

/** The firmware's number of a transport (`transport_e`): what the `conn` router's BIND carries as the provider. */
export const transportId = (provider: Provider): number => {
  const symbol = provider === 'BLE' ? 'TRANSPORT_BLE' : 'TRANSPORT_UART'
  const found = file.transports.find((entry) => entry.symbol === symbol)
  if (!found) throw new Error(`streams.generated.json lists no transport ${symbol}.`)
  return found.value
}

/** `Console (0)`: the UART endpoint the way the editor shows it (the planner reads the number in parentheses). */
export const uartEndpointText = (endpoint: number = boardSetup().uart.endpoint): string => `${boardSetup().uart.name} (${endpoint})`
