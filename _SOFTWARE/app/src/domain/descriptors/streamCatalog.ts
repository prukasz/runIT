import { DescriptorError } from './commandCatalog'
import type { GeneratedStreamsFile } from './generatedTypes'

/** One board → app stream: every frame on it starts with `header`. */
export interface StreamInfo {
  /** Connector name (`logs`, `errors`, `telemetry`, `interface`). */
  readonly name: string
  readonly header: number
  readonly connector: string
  readonly connectorId: number
  readonly alias: string
  readonly description: string
  /** BLE characteristics (16-bit UUIDs) the board binds this stream to. */
  readonly ble?: { readonly notify?: number; readonly write?: number }
  /** Frame cap of the connector, stream byte included. */
  readonly maxFrame: number
}

/** A characteristic the board creates at start (runit_board_ble_init). */
export interface BoardCharacteristic {
  readonly symbol: string
  readonly uuid: number
  readonly name: string
  readonly write: boolean
  readonly notify: boolean
  readonly indicate: boolean
  readonly txBufferSize: number
  readonly rxBufferSize: number
}

/** A connector binding the board makes at start (runit_board_connector_bindings_init, this sdkconfig). */
export interface BoardBinding {
  readonly connectorId: number
  readonly direction: 'tx' | 'rx'
  /** runit_data_provider_e symbol and value. */
  readonly provider: string
  readonly providerId: number
  /** BLE: characteristic UUID; UART: port. */
  readonly endpoint: number
  readonly endpointSymbol: string
  /** The Kconfig switch it depends on, if any. */
  readonly when?: string
}

/** What a board holds right after a restart, and the connector registry limits. */
export interface BoardSetup {
  readonly service: { readonly uuid: number; readonly symbol: string; readonly isPrimary: boolean }
  readonly characteristics: readonly BoardCharacteristic[]
  readonly bindings: readonly BoardBinding[]
  readonly limits: {
    /** Connector IDs 0..connectorsMax-1. */
    readonly connectorsMax: number
    /** Bindings per connector and direction. */
    readonly providersPerConnectorMax: number
    /** Name buffer, NUL included. */
    readonly nameMax: number
    /** Largest frame, stream byte included. */
    readonly frameMax: number
  }
}

/** The runIT GATT layout, from the board's connector bindings. */
export interface BleLayout {
  readonly service: number
  /** Where commands are written (the interface stream's write characteristic). */
  readonly commandWrite: number
  /** Every characteristic some stream notifies on. */
  readonly notify: readonly number[]
}

export interface StreamCatalog {
  readonly streams: readonly StreamInfo[]
  byHeader(header: number): StreamInfo | undefined
  /** Throws when the firmware publishes no stream of that name. */
  require(name: string): StreamInfo
  readonly ble: BleLayout
  readonly board: BoardSetup
}

const parseHex = (text: string, where: string, max: number): number => {
  const value = Number.parseInt(text, 16)
  if (!/^0x[0-9a-f]+$/i.test(text) || value > max) throw new DescriptorError(`${where}: '${text}' is not a hex value up to 0x${max.toString(16)}.`)
  return value
}

export const buildStreamCatalog = (file: GeneratedStreamsFile): StreamCatalog => {
  const streams: StreamInfo[] = file.streams.map((stream) => ({
    name: stream.name,
    header: parseHex(stream.header, `stream ${stream.name}`, 0xff),
    connector: stream.connector,
    connectorId: stream.connector_id,
    alias: stream.alias,
    description: stream.description,
    maxFrame: stream.max_frame,
    ble: stream.ble && {
      notify: stream.ble.notify === undefined ? undefined : parseHex(stream.ble.notify, `stream ${stream.name} notify`, 0xffff),
      write: stream.ble.write === undefined ? undefined : parseHex(stream.ble.write, `stream ${stream.name} write`, 0xffff),
    },
  }))
  const byHeader = new Map<number, StreamInfo>()
  const byName = new Map<string, StreamInfo>()
  for (const stream of streams) {
    if (byHeader.has(stream.header)) throw new DescriptorError(`Streams '${byHeader.get(stream.header)!.name}' and '${stream.name}' share header 0x${stream.header.toString(16)}.`)
    byHeader.set(stream.header, stream)
    byName.set(stream.name, stream)
  }
  const require = (name: string): StreamInfo => {
    const stream = byName.get(name)
    if (!stream) throw new DescriptorError(`The firmware publishes no '${name}' stream (streams.generated.json).`)
    return stream
  }
  const commandWrite = require('interface').ble?.write
  if (commandWrite === undefined) throw new DescriptorError("The 'interface' stream has no BLE write characteristic.")
  const notify = [...new Set(streams.flatMap((stream) => (stream.ble?.notify === undefined ? [] : [stream.ble.notify])))]
  const service = parseHex(file.ble.service, 'ble.service', 0xffff)
  const board: BoardSetup = {
    service: { uuid: service, symbol: file.ble.service_symbol, isPrimary: file.ble.is_primary },
    characteristics: file.ble.characteristics.map((char) => ({
      symbol: char.symbol,
      uuid: parseHex(char.uuid, `characteristic ${char.symbol}`, 0xffff),
      name: char.name,
      write: char.write,
      notify: char.notify,
      indicate: char.indicate,
      txBufferSize: char.tx_buffer_size,
      rxBufferSize: char.rx_buffer_size,
    })),
    bindings: file.bindings.map((binding) => {
      if (binding.direction !== 'tx' && binding.direction !== 'rx') throw new DescriptorError(`Binding of ${binding.connector}: direction '${binding.direction}'.`)
      return {
        connectorId: binding.connector_id,
        direction: binding.direction,
        provider: binding.provider,
        providerId: binding.provider_id,
        endpoint: binding.endpoint,
        endpointSymbol: binding.endpoint_symbol,
        ...(binding.when ? { when: binding.when } : {}),
      }
    }),
    limits: {
      connectorsMax: file.limits.connectors_max,
      providersPerConnectorMax: file.limits.providers_per_connector_max,
      nameMax: file.limits.name_max,
      frameMax: file.limits.frame_max,
    },
  }
  for (const binding of board.bindings) {
    if (!streams.some((stream) => stream.connectorId === binding.connectorId)) throw new DescriptorError(`The board binds connector ${binding.connectorId}, which is no system stream.`)
  }
  return {
    streams,
    byHeader: (header) => byHeader.get(header),
    require,
    ble: { service, commandWrite, notify },
    board,
  }
}
