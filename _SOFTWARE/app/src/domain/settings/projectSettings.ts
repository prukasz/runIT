import { boardSetup, hex, uartEndpointText } from './boardSetup'
import { DEFAULT_BLE_GENERAL } from './types'
import type { BleCharacteristicSettings, BleGeneralSettings, BleProfile, BleServiceSettings, ConnectorBindingSettings, ConnectorSettings, ProjectSettings } from './types'

/*
 * The settings in editor form, next to the firmware's view of them:
 *   - defaults: what a board holds after a restart (boardSetup) plus the editor's own fields;
 *   - refresh: a saved copy with the board's own entries taken from the board setup again, so an old copy never overrides the
 *     firmware (user services, user connectors and the bindings and pause of the system connectors are kept);
 *   - restore: put back what was removed from a system connector.
 */

const uuidValue = (text: string | undefined): number => Number.parseInt((text ?? '').trim().replace(/^0x/i, ''), 16)

export const isBoardService = (service: { readonly uuid: string; readonly system?: boolean } | null | undefined): boolean =>
  !!service && (service.system === true || uuidValue(service.uuid) === boardSetup().service.uuid)

/** The board's own characteristic: flagged `system` (they come from the board setup, in the locked runIT service). */
export const isBoardCharacteristic = (char: { readonly system?: boolean } | null | undefined): boolean => char?.system === true

/** The board's own GATT service in editor form: locked on the board, nothing of the user's in it. */
export const boardBleService = (): BleServiceSettings => {
  const { service, characteristics } = boardSetup()
  return {
    id: `sys-${service.symbol.toLowerCase()}`,
    name: service.name,
    uuid: hex(service.uuid, 4),
    system: true,
    isPrimary: service.isPrimary,
    characteristics: characteristics.map((char) => ({
      id: `sys-${char.symbol.toLowerCase()}`,
      name: char.name,
      uuid: hex(char.uuid, 4),
      system: true,
      write: char.write,
      notify: char.notify,
      indicate: char.indicate,
      framed: char.framed,
      txBufferSize: char.txBufferSize,
      rxBufferSize: char.rxBufferSize,
    })),
  }
}

const bindingId = (connectorKey: string, provider: string, endpoint: string): string => `b_${connectorKey}_${provider.toLowerCase()}_${endpoint}`

/** The board's connectors with their default bindings, in editor form. */
export const boardConnectors = (): ConnectorSettings[] => {
  const { streams, bindings } = boardSetup()
  return streams.map((stream) => ({
    id: stream.id,
    key: stream.key,
    name: stream.key,
    alias: stream.alias,
    stream: hex(stream.stream, 2),
    system: true,
    description: stream.description,
    isSuspended: false,
    bindings: bindings
      .filter((binding) => binding.connectorId === stream.id)
      .map((binding) => {
        const endpoint = binding.provider === 'UART' ? uartEndpointText(Number(binding.endpoint)) : binding.endpoint
        return { id: bindingId(stream.key, binding.provider, binding.endpoint), provider: binding.provider, endpoint }
      }),
  }))
}

export const defaultBleProfile = (): BleProfile => ({ name: 'runIT BLE GATT Profile', general: DEFAULT_BLE_GENERAL, services: [boardBleService()] })

export const defaultProjectSettings = (): ProjectSettings => ({ ble: defaultBleProfile(), connectors: boardConnectors() })

const userCharacteristic = (char: BleCharacteristicSettings): BleCharacteristicSettings => ({ ...char, system: false })

/** A saved profile (or none: the default) with the board's own service from the board setup. */
export const refreshProfile = (saved: BleProfile | undefined): BleProfile => {
  const defaults = defaultBleProfile()
  if (!saved) return defaults
  const userServices = saved.services
    .filter((service) => !isBoardService(service))
    .map((service) => ({ ...service, system: false, characteristics: service.characteristics.filter((char) => !isBoardCharacteristic(char)).map(userCharacteristic) }))
  return { name: saved.name || defaults.name, general: saved.general, services: [defaults.services[0]!, ...userServices] }
}

/** Saved connectors with the system ones as the board defines them; what the user may change on them (bindings, pause) is kept. */
export const refreshConnectors = (saved: readonly ConnectorSettings[] | undefined): ConnectorSettings[] => {
  const defaults = boardConnectors()
  if (!saved) return defaults
  const systemIds = new Set(defaults.map((connector) => connector.id))
  return [
    ...defaults.map((connector) => {
      const kept = saved.find((entry) => entry.system && entry.id === connector.id)
      return kept ? { ...connector, isSuspended: kept.isSuspended, bindings: kept.bindings.map((binding) => ({ ...binding })) } : connector
    }),
    ...saved.filter((connector) => !connector.system && !systemIds.has(connector.id)),
  ]
}

const bindingKey = (binding: Pick<ConnectorBindingSettings, 'provider' | 'endpoint'>): string => `${binding.provider}|${binding.endpoint.trim().toLowerCase()}`

/** The board's default bindings of a system connector that it lacks now (compared by transport and endpoint, not by ID). */
export const removedBindings = (connector: ConnectorSettings): ConnectorBindingSettings[] => {
  const fresh = boardConnectors().find((entry) => entry.id === connector.id)
  if (!connector.system || !fresh) return []
  const have = new Set(connector.bindings.map(bindingKey))
  return fresh.bindings.filter((binding) => !have.has(bindingKey(binding)))
}

/** A system connector as the board defines it (bindings, pause, name); other connectors are left as they are. */
export const restoreConnector = (connectors: readonly ConnectorSettings[], key: string): ConnectorSettings[] => {
  const fresh = boardConnectors()
  return connectors.map((connector) => {
    const original = connector.system ? fresh.find((entry) => entry.id === connector.id) : undefined
    return connector.key === key && original ? { ...original, bindings: original.bindings.map((binding) => ({ ...binding })) } : connector
  })
}

/** Put one removed default binding of a system connector back; nothing else on it changes. */
export const restoreBinding = (connectors: readonly ConnectorSettings[], key: string, bindingIdToRestore: string): ConnectorSettings[] =>
  connectors.map((connector) => {
    const binding = connector.key === key ? removedBindings(connector).find((entry) => entry.id === bindingIdToRestore) : undefined
    return binding ? { ...connector, bindings: [...connector.bindings, { ...binding }] } : connector
  })

/** Every system connector back as the board defines it, in the board's order, then the user's own connectors. */
export const restoreSystemConnectors = (connectors: readonly ConnectorSettings[]): ConnectorSettings[] => [...boardConnectors(), ...connectors.filter((connector) => !connector.system)]

/** What a restore would change: the fields of a system connector the user can edit. */
const fingerprint = (connector: ConnectorSettings) => ({ id: connector.id, alias: connector.alias, isSuspended: connector.isSuspended, bindings: connector.bindings.map(bindingKey).sort() })

/** One system connector differs from the board's definition of it (a transport removed, paused, renamed). */
export const connectorModified = (connector: ConnectorSettings): boolean => {
  const original = connector.system ? boardConnectors().find((entry) => entry.id === connector.id) : undefined
  return !!original && JSON.stringify(fingerprint(connector)) !== JSON.stringify(fingerprint(original))
}

/** The system connectors differ from the board's definition (something removed, changed or paused). */
export const connectorsModified = (connectors: readonly ConnectorSettings[]): boolean =>
  JSON.stringify(connectors.filter((connector) => connector.system).map(fingerprint)) !== JSON.stringify(boardConnectors().map(fingerprint))

/** The connectors that send on a BLE characteristic (a binding of provider BLE at its UUID). */
export const connectorsOnCharacteristic = (connectors: readonly ConnectorSettings[], uuid: string): ConnectorSettings[] =>
  connectors.filter((connector) => connector.bindings.some((binding) => binding.provider === 'BLE' && uuidValue(binding.endpoint) === uuidValue(uuid)))

// ---------------------------------------------------------------------------
// Reading saved copies: a copy that does not fit is dropped (the defaults take over), fields the editor does not know are ignored,
// fields it added since are defaulted, so a copy saved by an older version still opens.
// ---------------------------------------------------------------------------

const isRecord = (value: unknown): value is Record<string, unknown> => typeof value === 'object' && value !== null && !Array.isArray(value)
const text = (value: unknown, fallback = ''): string => (typeof value === 'string' ? value : fallback)
const flag = (value: unknown, fallback = false): boolean => (typeof value === 'boolean' ? value : fallback)
const whole = (value: unknown, fallback = 0): number => (typeof value === 'number' && Number.isInteger(value) && value >= 0 ? value : fallback)

const characteristicFrom = (value: unknown): BleCharacteristicSettings | undefined => {
  if (!isRecord(value) || typeof value.id !== 'string' || typeof value.uuid !== 'string') return undefined
  return {
    id: value.id,
    name: text(value.name, 'Characteristic'),
    uuid: value.uuid,
    ...(value.system === true ? { system: true } : {}),
    write: flag(value.write),
    notify: flag(value.notify),
    indicate: flag(value.indicate),
    framed: flag(value.framed),
    txBufferSize: whole(value.txBufferSize),
    rxBufferSize: whole(value.rxBufferSize),
  }
}

const serviceFrom = (value: unknown): BleServiceSettings | undefined => {
  if (!isRecord(value) || typeof value.id !== 'string' || typeof value.uuid !== 'string' || !Array.isArray(value.characteristics)) return undefined
  return {
    id: value.id,
    name: text(value.name, 'Service'),
    uuid: value.uuid,
    ...(value.system === true ? { system: true } : {}),
    isPrimary: flag(value.isPrimary, true),
    characteristics: value.characteristics.flatMap((char) => characteristicFrom(char) ?? []),
  }
}

const generalFrom = (value: unknown): BleGeneralSettings => {
  const source = isRecord(value) ? value : {}
  const out: Record<string, unknown> = {}
  for (const [key, fallback] of Object.entries(DEFAULT_BLE_GENERAL)) out[key] = typeof source[key] === typeof fallback ? source[key] : fallback
  if (!['just_works', 'passkey', 'mitm'].includes(out.securityMode as string)) out.securityMode = DEFAULT_BLE_GENERAL.securityMode
  return out as unknown as BleGeneralSettings
}

/** A saved BLE profile, refreshed from the board setup; undefined when the text is no profile. */
export const profileFromJson = (raw: string): BleProfile | undefined => {
  const value: unknown = JSON.parse(raw)
  if (!isRecord(value) || !Array.isArray(value.services)) return undefined
  return refreshProfile({ name: text(value.name), general: generalFrom(value.general), services: value.services.flatMap((service) => serviceFrom(service) ?? []) })
}

const hexByte = (value: unknown): string | undefined => (typeof value === 'string' && /^0x[0-9a-f]{1,2}$/i.test(value) ? value : undefined)

const connectorFrom = (value: unknown): ConnectorSettings | undefined => {
  if (!isRecord(value) || !Number.isInteger(value.id) || typeof value.key !== 'string' || !Array.isArray(value.bindings)) return undefined
  // `header` is what an older copy called the stream byte.
  const stream = hexByte(value.stream) ?? hexByte(value.header)
  if (!stream) return undefined
  const name = text(value.name, value.key)
  return {
    id: value.id as number,
    key: value.key,
    name,
    alias: text(value.alias, name),
    stream,
    system: flag(value.system),
    description: text(value.description),
    isSuspended: flag(value.isSuspended),
    bindings: value.bindings.flatMap((binding): ConnectorBindingSettings[] => {
      if (!isRecord(binding) || (binding.provider !== 'BLE' && binding.provider !== 'UART') || typeof binding.endpoint !== 'string') return []
      return [{ id: text(binding.id, bindingId(value.key as string, binding.provider, binding.endpoint)), provider: binding.provider, endpoint: binding.endpoint }]
    }),
  }
}

/** Saved connectors, the system ones refreshed from the board setup; undefined when the text is no list of connectors. */
export const connectorsFromJson = (raw: string): ConnectorSettings[] | undefined => {
  const value: unknown = JSON.parse(raw)
  if (!Array.isArray(value)) return undefined
  return refreshConnectors(value.flatMap((entry) => connectorFrom(entry) ?? []))
}
