import { runitEnumValue, runitStreamCatalog } from '../descriptors'
import type { StreamCatalog } from '../descriptors'
import { DEFAULT_BLE_GENERAL } from '../project'
import type { BleCharacteristicSettings, BleServiceSettings, ConnectorBindingSettings, ConnectorSettings, ProjectSettings } from '../project'
import { DEFAULT_LOGS, boardDefaultSettings } from '../upload'
import type { BleCharSpec, BleServiceSpec, ConnectorSpec, SettingsState } from '../upload'

/*
 * The project's settings section (BLE, data connectors) in editor form, next
 * to the firmware's view of them:
 *   - defaults: what a board holds after a restart without stored code
 *     (boardDefaultSettings) plus the editor's own fields;
 *   - refresh: a saved copy with the board's own entries taken from the
 *     descriptors again, so an old file never overrides the firmware;
 *   - from state: settings decoded from a board's stored code, as editor
 *     entries (recovery);
 *   - to state: what the settings planner and the code builder compare.
 */

const uuidValue = (text: string | undefined): number => Number.parseInt((text ?? '').trim().replace(/^0x/i, ''), 16)

/** Connector names the board carries on a characteristic by default. */
export const streamsOnCharacteristic = (uuid: number, streams: StreamCatalog = runitStreamCatalog()): string[] => {
  const ble = runitEnumValue('runit_data_provider_e', 'RUNIT_DATA_PROVIDER_BLE')
  return streams.streams
    .filter((stream) => streams.board.bindings.some((binding) => binding.providerId === ble && binding.endpoint === uuid && binding.connectorId === stream.connectorId))
    .map((stream) => stream.name)
}

export const isBoardService = (service: { readonly uuid: string; readonly system?: boolean } | null | undefined, streams: StreamCatalog = runitStreamCatalog()): boolean =>
  !!service && (service.system === true || uuidValue(service.uuid) === streams.board.service.uuid)

/** The board's own characteristic: flagged `system` (they come from the descriptors, in the locked board service). A board UUID elsewhere is a user's duplicate, not the board's. */
export const isBoardCharacteristic = (char: { readonly system?: boolean } | null | undefined): boolean => char?.system === true

const userCharacteristic = (char: BleCharSpec | BleCharacteristicSettings): BleCharacteristicSettings => ({
  format: 'RAW',
  ...char,
  system: false,
})

/** The board's own GATT service in editor form. */
export const boardBleService = (streams: StreamCatalog = runitStreamCatalog()): BleServiceSettings => {
  const service = boardDefaultSettings(streams).services[0]!
  return {
    ...service,
    system: true,
    advertised: true,
    characteristics: service.characteristics.map((char) => ({
      ...char,
      system: true,
      description: char.id.replace(/^sys-/, '').toUpperCase(),
      format: 'RAW' as const,
      connectorStream: streamsOnCharacteristic(uuidValue(char.uuid), streams)[0],
    })),
  }
}

const directionOf = (bindings: readonly ConnectorBindingSettings[]): ConnectorSettings['direction'] => {
  const tx = bindings.some((binding) => binding.direction !== 'RX')
  const rx = bindings.some((binding) => binding.direction !== 'TX')
  return tx && rx ? 'TX_RX' : rx ? 'RX' : 'TX'
}

/** The board's connectors with their default bindings, in editor form. */
export const boardConnectors = (streams: StreamCatalog = runitStreamCatalog()): ConnectorSettings[] =>
  boardDefaultSettings(streams).connectors.map((connector) => {
    const stream = streams.streams.find((entry) => entry.connectorId === connector.id)!
    return {
      ...connector,
      bindings: [...connector.bindings],
      alias: stream.alias,
      description: stream.description,
      direction: directionOf(connector.bindings),
      cMacro: stream.connector,
    }
  })

export const defaultProjectSettings = (streams: StreamCatalog = runitStreamCatalog()): ProjectSettings => ({
  ble: { name: 'runIT BLE GATT Profile', general: DEFAULT_BLE_GENERAL, services: [boardBleService(streams)] },
  connectors: boardConnectors(streams),
  logs: DEFAULT_LOGS,
})

/** A system connector as the board defines it, with what the user may change on it (bindings, suspended). */
const systemConnector = (fresh: ConnectorSettings, saved: Pick<ConnectorSpec, 'bindings' | 'isSuspended'> | undefined): ConnectorSettings =>
  saved ? { ...fresh, isSuspended: saved.isSuspended, bindings: saved.bindings.map((binding) => ({ ...binding })), direction: directionOf(saved.bindings) } : fresh

/**
 * Saved settings (or none: the defaults) with the board's own entries from the
 * descriptors (the board's service is locked: nothing of the user's in it).
 * Kept from the file: user services, user connectors, and bindings and
 * suspension of the system connectors.
 */
export const refreshSettings = (saved: ProjectSettings | undefined, streams: StreamCatalog = runitStreamCatalog()): ProjectSettings => {
  const defaults = defaultProjectSettings(streams)
  if (!saved) return defaults
  const fresh = defaults.ble.services[0]!
  const userServices = saved.ble.services
    .filter((service) => !isBoardService(service, streams))
    .map((service) => ({ ...service, system: false, characteristics: service.characteristics.filter((char) => !isBoardCharacteristic(char)).map(userCharacteristic) }))

  const systemIds = new Set(defaults.connectors.map((connector) => connector.id))
  const connectors = [
    ...defaults.connectors.map((connector) => systemConnector(connector, saved.connectors.find((entry) => entry.system && entry.id === connector.id))),
    ...saved.connectors.filter((connector) => !connector.system && !systemIds.has(connector.id)),
  ]
  return {
    ble: { name: saved.ble.name || defaults.ble.name, general: saved.ble.general, services: [fresh, ...userServices] },
    connectors,
    logs: saved.logs,
  }
}

const bindingKey = (binding: Pick<ConnectorBindingSettings, 'provider' | 'endpoint' | 'direction'>): string => `${binding.provider}|${binding.endpoint.trim().toLowerCase()}|${binding.direction}`

/** The board's default bindings of a system connector that it lacks now (compared by transport, endpoint and direction, not by ID). */
export const removedBindings = (connector: ConnectorSettings, streams: StreamCatalog = runitStreamCatalog()): ConnectorBindingSettings[] => {
  const fresh = boardConnectors(streams).find((entry) => entry.id === connector.id)
  if (!connector.system || !fresh) return []
  const have = new Set(connector.bindings.map(bindingKey))
  return fresh.bindings.filter((binding) => !have.has(bindingKey(binding)))
}

/** A system connector as the board defines it (bindings, suspension, name, size); other connectors are left as they are. */
export const restoreConnector = (connectors: readonly ConnectorSettings[], key: string, streams: StreamCatalog = runitStreamCatalog()): ConnectorSettings[] => {
  const fresh = boardConnectors(streams)
  return connectors.map((connector) => {
    const original = connector.system ? fresh.find((entry) => entry.id === connector.id) : undefined
    return connector.key === key && original ? { ...original, bindings: original.bindings.map((binding) => ({ ...binding })) } : connector
  })
}

/** Put one removed default binding of a system connector back; nothing else on it changes. */
export const restoreBinding = (connectors: readonly ConnectorSettings[], key: string, bindingId: string, streams: StreamCatalog = runitStreamCatalog()): ConnectorSettings[] =>
  connectors.map((connector) => {
    const binding = connector.key === key ? removedBindings(connector, streams).find((entry) => entry.id === bindingId) : undefined
    return binding ? { ...connector, bindings: [...connector.bindings, { ...binding }], direction: directionOf([...connector.bindings, binding]) } : connector
  })

/** Every system connector back as the board defines it, in the board's order, then the user's own connectors. */
export const restoreSystemConnectors = (connectors: readonly ConnectorSettings[], streams: StreamCatalog = runitStreamCatalog()): ConnectorSettings[] => [
  ...boardConnectors(streams),
  ...connectors.filter((connector) => !connector.system),
]

/** One system connector differs from the board's definition of it (a transport removed, paused, renamed, resized). */
export const connectorModified = (connector: ConnectorSettings, streams: StreamCatalog = runitStreamCatalog()): boolean => {
  const original = connector.system ? boardConnectors(streams).find((entry) => entry.id === connector.id) : undefined
  return !!original && JSON.stringify(connectorFingerprint(connector)) !== JSON.stringify(connectorFingerprint(original))
}

/** The system connectors differ from the board's definition (something removed, changed or paused). */
export const connectorsModified = (connectors: readonly ConnectorSettings[], streams: StreamCatalog = runitStreamCatalog()): boolean =>
  JSON.stringify(connectors.filter((connector) => connector.system).map(connectorFingerprint)) !== JSON.stringify(boardConnectors(streams).map(connectorFingerprint))

/** What a restore would change: the fields of a system connector the user can edit. */
const connectorFingerprint = (connector: ConnectorSettings) => ({
  id: connector.id,
  alias: connector.alias,
  maxPacketLen: connector.maxPacketLen,
  isSuspended: connector.isSuspended,
  bindings: connector.bindings.map(bindingKey).sort(),
})

/**
 * Settings decoded from a stored code (board defaults + its frames) as editor
 * settings. What the board doesn't hold (descriptions, formats, aliases) gets
 * defaults; `general` is kept from `base` (the board has no commands for it).
 */
export const settingsFromState = (state: SettingsState, base: ProjectSettings = defaultProjectSettings(), streams: StreamCatalog = runitStreamCatalog()): ProjectSettings => {
  const defaults = defaultProjectSettings(streams)
  const fresh = defaults.ble.services[0]!
  const services = state.services.map((service: BleServiceSpec): BleServiceSettings => {
    if (isBoardService(service, streams)) return fresh
    return { id: service.id, name: service.name, uuid: service.uuid, system: false, isPrimary: service.isPrimary, advertised: true, characteristics: service.characteristics.map(userCharacteristic) }
  })
  const connectors = state.connectors.map((connector): ConnectorSettings => {
    const system = defaults.connectors.find((entry) => entry.id === connector.id)
    if (connector.system && system) return systemConnector(system, connector)
    const bindings = connector.bindings.map((binding) => ({ ...binding }))
    return { ...connector, bindings, alias: connector.name, description: '', direction: directionOf(bindings), cMacro: `SYS_DATA_CONNECTOR_APP_${connector.id}` }
  })
  return { ble: { ...base.ble, services }, connectors, logs: state.logs }
}

/** What the settings planner and the stored code builder compare. */
export const settingsState = (settings: ProjectSettings): SettingsState => ({ services: settings.ble.services, connectors: settings.connectors, logs: settings.logs })
