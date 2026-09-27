import { packCommand, runitEnumValue, runitStreamCatalog } from '../descriptors'
import type { BleLayout, BoardSetup, CommandCatalog } from '../descriptors'
import type { UploadDiagnostic, UploadPlan, UploadStep } from './bundle'

/*
 * BLE GATT and data connector settings → the commands that turn what the
 * board has (`from`) into what the editor shows (`to`).
 *
 * The board keeps none of this across a restart and can't report it, so the
 * caller tracks `from`: the board defaults after connecting, then whatever
 * was last applied. Only differences are sent, in an order the firmware
 * accepts:
 *   1. connector removes and unbinds (before the characteristics they use go)
 *   2. BLE characteristic and service removes
 *   3. BLE service and characteristic creates
 *   4. connector creates / reconfigures, binds, suspend / resume
 * A changed characteristic or service is removed and created again (the
 * firmware has no edit). System services, characteristics and connectors are
 * never removed; the command link (interface stream) must stay bound.
 */

export interface BleCharSpec {
  readonly id: string
  readonly name: string
  readonly uuid: string
  readonly system?: boolean
  readonly read: boolean
  readonly write: boolean
  readonly writeNoResponse: boolean
  readonly notify: boolean
  readonly indicate: boolean
  readonly txBufferSize: number
  readonly rxBufferSize: number
}

export interface BleServiceSpec {
  readonly id: string
  readonly name: string
  readonly uuid: string
  readonly system?: boolean
  readonly isPrimary: boolean
  readonly characteristics: readonly BleCharSpec[]
}

export interface ConnectorBindingSpec {
  readonly id: string
  readonly provider: 'BLE' | 'UART'
  /** BLE: characteristic UUID (`0xFFE1`); UART: port number (`0`, `Console (0)`). */
  readonly endpoint: string
  readonly direction: 'TX' | 'RX' | 'TX_RX'
}

export interface ConnectorSpec {
  readonly id: number
  readonly key: string
  readonly name: string
  readonly header: string
  readonly system: boolean
  readonly maxPacketLen: number
  readonly isSuspended: boolean
  readonly bindings: readonly ConnectorBindingSpec[]
}

export interface SettingsState {
  readonly services: readonly BleServiceSpec[]
  readonly connectors: readonly ConnectorSpec[]
}

export interface SettingsIds {
  /** runit_data_provider_e values. */
  readonly providers: { readonly BLE: number; readonly UART: number }
  /** First connector ID the user may create (SYS_DATA_CONNECTOR_APP_BASE). */
  readonly appConnectorBase: number
  /** Connector ID of the interface stream (the app's command link). */
  readonly interfaceConnector: number
  /** Connector registry limits (CONFIG_SYS_DATA_CONNECTOR_*). */
  readonly limits: BoardSetup['limits']
}

/** The IDs above from the firmware descriptors (enums.json, streams.generated.json). */
export const runitSettingsIds = (): SettingsIds => ({
  providers: {
    BLE: runitEnumValue('runit_data_provider_e', 'RUNIT_DATA_PROVIDER_BLE'),
    UART: runitEnumValue('runit_data_provider_e', 'RUNIT_DATA_PROVIDER_UART'),
  },
  appConnectorBase: runitEnumValue('sys_data_connector_id_e', 'SYS_DATA_CONNECTOR_APP_BASE'),
  interfaceConnector: runitStreamCatalog().require('interface').connectorId,
  limits: runitStreamCatalog().board.limits,
})

interface Context {
  readonly catalog: CommandCatalog
  readonly layout: BleLayout
  readonly ids: SettingsIds
  readonly diagnostics: UploadDiagnostic[]
}

const hex4 = (value: number): string => `0x${value.toString(16).padStart(4, '0').toUpperCase()}`

const parseUuid16 = (text: string): number | undefined => {
  const match = /^\s*(?:0x)?([0-9a-f]{1,4})\s*$/i.exec(text)
  if (!match) return undefined
  const value = Number.parseInt(match[1]!, 16)
  return value === 0 ? undefined : value
}

const parseByteHex = (text: string): number | undefined => {
  const match = /^\s*0x([0-9a-f]{1,2})\s*$/i.exec(text)
  return match ? Number.parseInt(match[1]!, 16) : undefined
}

const parseEndpoint = (binding: ConnectorBindingSpec): number | undefined => {
  if (binding.provider === 'BLE') return parseUuid16(binding.endpoint)
  const match = /\((\d+)\)\s*$/.exec(binding.endpoint) ?? /^\s*(\d+)\s*$/.exec(binding.endpoint)
  return match ? Number(match[1]) : undefined
}

const step = (ctx: Context, commandId: string, values: Record<string, number | string>, label: string): UploadStep => {
  const command = ctx.catalog.get(commandId)
  if (!command) throw new Error(`The firmware has no command ${commandId}.`)
  return { label, frame: packCommand(command, values) }
}

// ---------------------------------------------------------------------------
// BLE
// ---------------------------------------------------------------------------

interface CharEntry {
  readonly spec: BleCharSpec
  readonly service: BleServiceSpec
  readonly uuid: number
  readonly serviceUuid: number
  readonly system: boolean
  /** Everything the firmware stores; equal signatures need no command. */
  readonly signature: string
}

interface ServiceEntry {
  readonly spec: BleServiceSpec
  readonly uuid: number
  readonly system: boolean
  readonly signature: string
}

const isSystemService = (ctx: Context, service: BleServiceSpec, uuid: number | undefined): boolean => service.system === true || uuid === ctx.layout.service

const isSystemChar = (ctx: Context, char: BleCharSpec, uuid: number | undefined): boolean =>
  char.system === true || (uuid !== undefined && (uuid === ctx.layout.commandWrite || ctx.layout.notify.includes(uuid)))

/** Index a GATT profile; `check` reports problems (only for the target state). */
const indexBle = (ctx: Context, services: readonly BleServiceSpec[], check: boolean) => {
  const serviceById = new Map<string, ServiceEntry>()
  const charById = new Map<string, CharEntry>()
  const serviceUuids = new Map<number, string>()
  const charUuids = new Map<number, string>()
  const problem = (message: string, subjectId: string) => { if (check) ctx.diagnostics.push({ severity: 'error', message, subjectId }) }

  for (const service of services) {
    const uuid = parseUuid16(service.uuid)
    if (uuid === undefined) {
      problem(`Service '${service.name}': '${service.uuid}' is not a 16-bit UUID (0x0001–0xFFFF).`, service.id)
      continue
    }
    const system = isSystemService(ctx, service, uuid)
    if (serviceUuids.has(uuid)) problem(`Services '${serviceUuids.get(uuid)}' and '${service.name}' share UUID ${hex4(uuid)}.`, service.id)
    serviceUuids.set(uuid, service.name)
    // Keyed by UUID: the board identifies services and characteristics by UUID, not by the editor's IDs.
    serviceById.set(hex4(uuid), { spec: service, uuid, system, signature: `${uuid}|${service.isPrimary ? 1 : 0}` })

    for (const char of service.characteristics) {
      const charUuid = parseUuid16(char.uuid)
      if (charUuid === undefined) {
        problem(`Characteristic '${char.name}': '${char.uuid}' is not a 16-bit UUID (0x0001–0xFFFF).`, char.id)
        continue
      }
      // The firmware looks characteristics up by UUID alone, across services.
      if (charUuids.has(charUuid)) problem(`Characteristics '${charUuids.get(charUuid)}' and '${char.name}' share UUID ${hex4(charUuid)}: the board finds characteristics by UUID alone.`, char.id)
      charUuids.set(charUuid, char.name)
      const charSystem = isSystemChar(ctx, char, charUuid)
      if (!charSystem && check) {
        // What the board can express (sys_ble_stack.c): no READ flag, and is_write allows both write kinds.
        if (char.read) ctx.diagnostics.push({ severity: 'warning', message: `Characteristic '${char.name}': the board doesn't make characteristics readable; 'read' is not sent.`, subjectId: char.id })
        if (char.write !== char.writeNoResponse) ctx.diagnostics.push({ severity: 'warning', message: `Characteristic '${char.name}': the board allows write and write-without-response together; both will be on.`, subjectId: char.id })
        if (!Number.isInteger(char.txBufferSize) || char.txBufferSize < 0 || !Number.isInteger(char.rxBufferSize) || char.rxBufferSize < 0) problem(`Characteristic '${char.name}': buffer sizes are whole numbers of bytes.`, char.id)
        if ((char.notify || char.indicate) && char.txBufferSize === 0) ctx.diagnostics.push({ severity: 'warning', message: `Characteristic '${char.name}' notifies but has no TX buffer: the board can't send on it.`, subjectId: char.id })
        if ((char.write || char.writeNoResponse) && char.rxBufferSize === 0) ctx.diagnostics.push({ severity: 'warning', message: `Characteristic '${char.name}' is writable but has no RX buffer: writes are dropped.`, subjectId: char.id })
      }
      const isWrite = char.write || char.writeNoResponse
      charById.set(hex4(charUuid), {
        spec: char,
        service,
        uuid: charUuid,
        serviceUuid: uuid,
        system: charSystem,
        signature: [uuid, charUuid, isWrite ? 1 : 0, char.indicate ? 1 : 0, char.notify ? 1 : 0, char.txBufferSize, char.rxBufferSize, char.name].join('|'),
      })
    }
  }
  return { serviceById, charById }
}

const planBle = (ctx: Context, from: readonly BleServiceSpec[], to: readonly BleServiceSpec[]): { removes: UploadStep[]; creates: UploadStep[] } => {
  const before = indexBle(ctx, from, false)
  const after = indexBle(ctx, to, true)
  const removes: UploadStep[] = []
  const creates: UploadStep[] = []

  // System entries are the board's own: a change is not sent, a removal never.
  for (const [id, entry] of before.serviceById) {
    if (!entry.system) continue
    const next = after.serviceById.get(id)
    if (!next) ctx.diagnostics.push({ severity: 'error', message: `System service '${entry.spec.name}' can't be removed.`, subjectId: entry.spec.id })
    else if (next.signature !== entry.signature) ctx.diagnostics.push({ severity: 'warning', message: `System service '${entry.spec.name}' is fixed on the board; the change is not sent.`, subjectId: entry.spec.id })
  }
  for (const [id, entry] of before.charById) {
    if (!entry.system) continue
    const next = after.charById.get(id)
    if (!next) ctx.diagnostics.push({ severity: 'error', message: `System characteristic '${entry.spec.name}' can't be removed: the app's link runs on it.`, subjectId: entry.spec.id })
    else if (next.signature !== entry.signature) ctx.diagnostics.push({ severity: 'warning', message: `System characteristic '${entry.spec.name}' is fixed on the board; the change is not sent.`, subjectId: entry.spec.id })
  }
  for (const [id, entry] of after.serviceById) {
    if (entry.system && !before.serviceById.has(id) && entry.uuid !== ctx.layout.service) ctx.diagnostics.push({ severity: 'warning', message: `Service '${entry.spec.name}' is marked system but the board doesn't have it; it is not sent.`, subjectId: entry.spec.id })
  }

  const serviceGone = (id: string): boolean => {
    const old = before.serviceById.get(id)
    if (!old || old.system) return false
    const next = after.serviceById.get(id)
    return !next || next.signature !== old.signature
  }
  const serviceNew = (id: string): boolean => {
    const next = after.serviceById.get(id)
    if (!next || next.system) return false
    const old = before.serviceById.get(id)
    return !old || old.signature !== next.signature
  }

  for (const [id, old] of before.charById) {
    if (old.system) continue
    const ownerGone = [...before.serviceById.entries()].some(([serviceId, entry]) => entry.spec === old.service && serviceGone(serviceId))
    if (ownerGone) continue // the service remove takes its characteristics along
    const next = after.charById.get(id)
    if (!next || next.signature !== old.signature) removes.push(step(ctx, 'packet_settings_ble_char_remove_t', { service_uuid: old.serviceUuid, uuid: old.uuid }, `ble remove char ${hex4(old.uuid)}`))
  }
  for (const [id, old] of before.serviceById) {
    if (serviceGone(id)) removes.push(step(ctx, 'packet_settings_ble_service_remove_t', { uuid: old.uuid }, `ble remove service ${hex4(old.uuid)}`))
  }
  for (const [id, next] of after.serviceById) {
    if (serviceNew(id)) creates.push(step(ctx, 'packet_settings_ble_service_create_t', { uuid: next.uuid, is_primary: next.spec.isPrimary ? 1 : 0 }, `ble create service ${hex4(next.uuid)}`))
  }
  for (const [id, next] of after.charById) {
    if (next.system) continue
    const ownerNew = [...after.serviceById.entries()].some(([serviceId, entry]) => entry.spec === next.service && serviceNew(serviceId))
    const old = before.charById.get(id)
    if (!ownerNew && old && old.signature === next.signature) continue
    const { spec } = next
    creates.push(step(ctx, 'packet_settings_ble_char_create_t', {
      service_uuid: next.serviceUuid,
      uuid: next.uuid,
      is_write: spec.write || spec.writeNoResponse ? 1 : 0,
      is_indicate: spec.indicate ? 1 : 0,
      is_notify: spec.notify ? 1 : 0,
      tx_buffer_size: spec.txBufferSize,
      rx_buffer_size: spec.rxBufferSize,
      name: spec.name,
    }, `ble create char ${hex4(next.uuid)}`))
  }
  return { removes, creates }
}

// ---------------------------------------------------------------------------
// Data connectors
// ---------------------------------------------------------------------------

type Direction = 'TX' | 'RX'

/** `TX:1` → endpoint, one per provider and direction (the firmware keeps one). */
const bindingMap = (ctx: Context, connector: ConnectorSpec, check: boolean): Map<string, number> => {
  const map = new Map<string, number>()
  for (const binding of connector.bindings) {
    const endpoint = parseEndpoint(binding)
    if (endpoint === undefined) {
      if (check) ctx.diagnostics.push({ severity: 'error', message: `Connector '${connector.name}': '${binding.endpoint}' is not a ${binding.provider === 'BLE' ? 'characteristic UUID' : 'UART port number'}.`, subjectId: connector.key })
      continue
    }
    const directions: Direction[] = binding.direction === 'TX_RX' ? ['TX', 'RX'] : [binding.direction]
    for (const direction of directions) {
      const key = `${direction}:${ctx.ids.providers[binding.provider]}`
      if (map.has(key) && map.get(key) !== endpoint && check) ctx.diagnostics.push({ severity: 'error', message: `Connector '${connector.name}' has two ${binding.provider} ${direction} bindings; the board keeps one per provider and direction.`, subjectId: connector.key })
      map.set(key, endpoint)
    }
  }
  return map
}

const planConnectors = (ctx: Context, from: readonly ConnectorSpec[], to: readonly ConnectorSpec[]): { early: UploadStep[]; late: UploadStep[] } => {
  const early: UploadStep[] = []
  const late: UploadStep[] = []
  const before = new Map(from.map((connector) => [connector.id, connector]))
  const after = new Map<number, ConnectorSpec>()
  const headers = new Map<number, string>()
  const providerName = (id: number): string => (id === ctx.ids.providers.BLE ? 'BLE' : id === ctx.ids.providers.UART ? 'UART' : `provider ${id}`)

  for (const connector of to) {
    const error = (message: string) => ctx.diagnostics.push({ severity: 'error', message: `Connector '${connector.name}': ${message}`, subjectId: connector.key })
    if (after.has(connector.id)) error(`ID ${connector.id} is used twice.`)
    after.set(connector.id, connector)
    const header = parseByteHex(connector.header)
    if (header === undefined) error(`'${connector.header}' is not a one-byte hex header.`)
    else if (headers.has(header)) error(`header ${connector.header} is also used by '${headers.get(header)}'.`)
    else headers.set(header, connector.name)
    if (!connector.system && connector.id < ctx.ids.appConnectorBase) error(`user connectors take IDs from ${ctx.ids.appConnectorBase}.`)
    const { limits } = ctx.ids
    if (connector.id < 0 || connector.id >= limits.connectorsMax) error(`IDs go up to ${limits.connectorsMax - 1} (CONFIG_SYS_DATA_CONNECTOR_MAX).`)
    if (!connector.system && (!Number.isInteger(connector.maxPacketLen) || connector.maxPacketLen < 2 || connector.maxPacketLen > limits.frameMax)) error(`the maximum packet length is 2..${limits.frameMax} bytes (CONFIG_SYS_DATA_CONNECTOR_FRAME_MAX).`)
    if (!connector.system && new TextEncoder().encode(connector.name).byteLength > limits.nameMax - 1) ctx.diagnostics.push({ severity: 'warning', message: `Connector '${connector.name}': the board keeps ${limits.nameMax - 1} bytes of the name.`, subjectId: connector.key })
  }

  // The command link: the interface stream must stay on the characteristics this app talks through.
  const link = after.get(ctx.ids.interfaceConnector)
  if (!link) ctx.diagnostics.push({ severity: 'error', message: "The interface (commands) connector can't be removed: the app's link runs on it." })
  else {
    const map = bindingMap(ctx, link, false)
    const ble = ctx.ids.providers.BLE
    if (map.get(`RX:${ble}`) !== ctx.layout.commandWrite) ctx.diagnostics.push({ severity: 'error', message: `The interface connector must stay bound to BLE RX ${hex4(ctx.layout.commandWrite)}: the app writes its commands there.`, subjectId: link.key })
    const tx = map.get(`TX:${ble}`)
    if (tx === undefined || !ctx.layout.notify.includes(tx)) ctx.diagnostics.push({ severity: 'error', message: `The interface connector must stay bound to BLE TX on ${ctx.layout.notify.map(hex4).join(' / ')}: the app reads answers there.`, subjectId: link.key })
  }

  for (const [id, old] of before) {
    if (after.has(id)) continue
    if (old.system) ctx.diagnostics.push({ severity: 'error', message: `System connector '${old.name}' can't be removed.`, subjectId: old.key })
    else early.push(step(ctx, 'packet_settings_data_connector_remove_t', { id }, `connector remove ${old.name}`))
  }

  for (const [id, next] of after) {
    const old = before.get(id)
    const nextBindings = bindingMap(ctx, next, true)
    for (const direction of ['TX', 'RX'] as const) {
      const count = [...nextBindings.keys()].filter((key) => key.startsWith(`${direction}:`)).length
      if (count > ctx.ids.limits.providersPerConnectorMax) ctx.diagnostics.push({ severity: 'error', message: `Connector '${next.name}' has ${count} ${direction} bindings, the board keeps ${ctx.ids.limits.providersPerConnectorMax} (CONFIG_SYS_DATA_CONNECTOR_PROVIDERS_MAX).`, subjectId: next.key })
    }
    const oldBindings = old ? bindingMap(ctx, old, false) : new Map<string, number>()
    const configChanged = !old || old.name !== next.name || old.header !== next.header || old.maxPacketLen !== next.maxPacketLen

    if (configChanged) {
      if (next.system) {
        if (old) ctx.diagnostics.push({ severity: 'warning', message: `System connector '${next.name}': name, header and packet length are fixed on the board; the change is not sent.`, subjectId: next.key })
      } else {
        const header = parseByteHex(next.header)
        if (header !== undefined) late.push(step(ctx, 'packet_settings_data_connector_create_t', { id, header, max_packet_len: next.maxPacketLen, name: next.name }, `connector ${old ? 'update' : 'create'} ${next.name}`))
      }
    }

    const txLeft = [...nextBindings.keys()].filter((key) => key.startsWith('TX:')).length
    for (const [key, endpoint] of oldBindings) {
      if (nextBindings.has(key)) continue
      const [direction, provider] = key.split(':') as [Direction, string]
      if (next.system && direction === 'TX' && txLeft === 0) {
        ctx.diagnostics.push({ severity: 'error', message: `System connector '${next.name}' needs at least one TX binding.`, subjectId: next.key })
        continue
      }
      early.push(step(ctx, direction === 'TX' ? 'packet_settings_data_connector_tx_remove_t' : 'packet_settings_data_connector_rx_remove_t', { connector_id: id, provider_id: Number(provider) }, `connector ${next.name} unbind ${direction} ${providerName(Number(provider))} ${hex4(endpoint)}`))
    }
    for (const [key, endpoint] of nextBindings) {
      if (oldBindings.get(key) === endpoint) continue
      const [direction, provider] = key.split(':') as [Direction, string]
      // Binding again replaces the endpoint: no unbind needed for a move.
      late.push(step(ctx, direction === 'TX' ? 'packet_settings_data_connector_tx_add_t' : 'packet_settings_data_connector_rx_add_t', { connector_id: id, provider_id: Number(provider), provider_param: endpoint }, `connector ${next.name} bind ${direction} ${providerName(Number(provider))} ${hex4(endpoint)}`))
    }

    const wasSuspended = old?.isSuspended ?? false
    if (next.isSuspended !== wasSuspended) {
      const hasRx = [...nextBindings.keys()].some((key) => key.startsWith('RX:'))
      if (next.isSuspended && next.system && hasRx) ctx.diagnostics.push({ severity: 'error', message: `System connector '${next.name}' takes commands in and can't be suspended.`, subjectId: next.key })
      else late.push(step(ctx, next.isSuspended ? 'packet_settings_data_connector_suspend_t' : 'packet_settings_data_connector_resume_t', { id }, `connector ${next.isSuspended ? 'suspend' : 'resume'} ${next.name}`))
    }
  }
  return { early, late }
}

/** Commands that turn `from` into `to` on the board. `ok` false: nothing may be sent. */
export const planSettingsUpload = (catalog: CommandCatalog, layout: BleLayout, ids: SettingsIds, from: SettingsState, to: SettingsState): UploadPlan => {
  const ctx: Context = { catalog, layout, ids, diagnostics: [] }
  let steps: UploadStep[] = []
  try {
    const ble = planBle(ctx, from.services, to.services)
    const connectors = planConnectors(ctx, from.connectors, to.connectors)
    steps = [...connectors.early, ...ble.removes, ...ble.creates, ...connectors.late]
  } catch (error) {
    ctx.diagnostics.push({ severity: 'error', message: error instanceof Error ? error.message : String(error) })
  }
  const ok = !ctx.diagnostics.some((entry) => entry.severity === 'error')
  return { ok, steps: ok ? steps : [], diagnostics: ctx.diagnostics }
}
