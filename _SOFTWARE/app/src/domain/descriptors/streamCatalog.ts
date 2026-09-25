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
  return {
    streams,
    byHeader: (header) => byHeader.get(header),
    require,
    ble: { service: parseHex(file.ble.service, 'ble.service', 0xffff), commandWrite, notify },
  }
}
