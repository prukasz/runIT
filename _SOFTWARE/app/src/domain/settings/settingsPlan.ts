import { routerKey, routerOp } from '../../backend/catalog'
import { transportId } from './boardSetup'
import { configOp, encodeConfig, opRecord } from './contractConfig'
import type { ConfigOp, ConfigValues } from './contractConfig'
import { boardConnectors, defaultBleProfile, isBoardCharacteristic, isBoardService } from './projectSettings'
import type { BleCharacteristicSettings, BleProfile, BleServiceSettings, ConnectorBindingSettings, ConnectorSettings } from './types'

/*
 * Saving the settings = sending the router frames that bring the board to them. The frames are built from the generated contracts
 * (the argument structs and op ids come from `contracts.generated.json`), so a field added in C reaches the board with no change here.
 *
 *   - settings ops (`@settings` in C: log, power): always sent, the board applies a whole struct;
 *   - `ble` router: services and characteristics are created at runtime, staged, then APPLY puts them live;
 *   - `conn` router: user connectors are created and bound to a transport endpoint.
 *
 * The board cannot list its GATT table, so what was sent last is the baseline: a save sends only the difference. Without a baseline
 * (first save of the session, after a drop, after a refusal) every user entry is removed first (a refusal of those is ignored) and
 * created again, which is right for a board that already holds some of them.
 */

export interface SettingsStep {
  readonly label: string
  /** Router key and op id. */
  readonly keys: readonly [number, number]
  readonly payload?: Uint8Array
  /** A removal that may find nothing to remove: the board refusing it is not a failure. */
  readonly tolerant?: boolean
  /** Part of a record being stored: the board only keeps it (or runs it, per the record's `execute`). */
  readonly recorded?: boolean
  /** Makes the board drop the link and come back (BLE APPLY with a client connected). */
  readonly reconnects?: boolean
}

/** What a save sends the board. */
export interface SettingsSnapshot {
  readonly ble: BleProfile
  readonly connectors: readonly ConnectorSettings[]
}

/** A record to store: the frames it holds, each `[router key][op][arguments]` (none: the record is removed). */
export interface RecordFrames {
  readonly label: string
  readonly id: number
  readonly frames: readonly Uint8Array[]
}

/** The values of the settings ops of one router (`ROUTER_KEY_POWER`), by op symbol. */
export interface RouterValues {
  readonly ops: readonly ConfigOp[]
  readonly values: Readonly<Record<string, ConfigValues>>
}

const number = (text: string): number => Number(text.trim())
/** The number a binding's endpoint stands for: a BLE characteristic UUID (`0xEE01`) or the UART endpoint the editor words as `Console (0)`. */
const endpointOf = (binding: ConnectorBindingSettings): number => {
  const text = binding.endpoint.trim()
  const inBrackets = /\((\d+)\)\s*$/.exec(text)
  return inBrackets ? Number(inBrackets[1]) : number(text)
}
const flag = (on: boolean): number => (on ? 1 : 0)
const nameTail = (name: string): Uint8Array => Uint8Array.of(...new TextEncoder().encode(name), 0)

const concat = (head: Uint8Array, tail: Uint8Array): Uint8Array => {
  const out = new Uint8Array(head.length + tail.length)
  out.set(head)
  out.set(tail, head.length)
  return out
}

/** One op of a router with its fields by name; `tail` (a name) follows the struct. */
const frame = (label: string, routerSymbol: string, opSymbol: string, values?: ConfigValues, tail?: Uint8Array, tolerant?: boolean): SettingsStep => {
  const keys = [routerKey(routerSymbol), routerOp(routerSymbol, opSymbol)] as const
  if (!values) return { label, keys }
  const op = configOp(routerSymbol, opSymbol)
  const head = encodeConfig(op, values)
  return { label, keys, payload: tail ? concat(head, tail) : head, ...(tolerant ? { tolerant } : {}) }
}

const BLE = 'ROUTER_KEY_BLE'
const CONN = 'ROUTER_KEY_CONN'

const userServices = (profile: BleProfile | undefined): BleServiceSettings[] => (profile?.services ?? []).filter((service) => !isBoardService(service))
const userCharacteristics = (service: BleServiceSettings): BleCharacteristicSettings[] => service.characteristics.filter((char) => !isBoardCharacteristic(char))
const userConnectors = (connectors: readonly ConnectorSettings[] | undefined): ConnectorSettings[] => (connectors ?? []).filter((connector) => !connector.system)

const sameChar = (a: BleCharacteristicSettings, b: BleCharacteristicSettings): boolean =>
  number(a.uuid) === number(b.uuid) && a.name === b.name && a.write === b.write && a.notify === b.notify && a.indicate === b.indicate && a.framed === b.framed &&
  a.txBufferSize === b.txBufferSize && a.rxBufferSize === b.rxBufferSize

const svcCreate = (service: BleServiceSettings): SettingsStep =>
  frame(`Create service ${service.uuid}`, BLE, 'OP_SVC_CREATE', { uuid: number(service.uuid), is_primary: flag(service.isPrimary) })

const charCreate = (service: BleServiceSettings, char: BleCharacteristicSettings): SettingsStep =>
  frame(`Create characteristic ${char.uuid}`, BLE, 'OP_CHAR_CREATE', {
    svc_uuid: number(service.uuid), uuid: number(char.uuid), is_write: flag(char.write), is_indicate: flag(char.indicate), is_notify: flag(char.notify),
    is_framed: flag(char.framed), tx_buffer_size: char.txBufferSize, rx_buffer_size: char.rxBufferSize,
  }, nameTail(char.name))

const svcRemove = (service: BleServiceSettings, tolerant = false): SettingsStep =>
  frame(`Remove service ${service.uuid}`, BLE, 'OP_SVC_REMOVE', { uuid: number(service.uuid) }, undefined, tolerant)

const charRemove = (service: BleServiceSettings, char: BleCharacteristicSettings): SettingsStep =>
  frame(`Remove characteristic ${char.uuid}`, BLE, 'OP_CHAR_REMOVE', { svc_uuid: number(service.uuid), uuid: number(char.uuid) })

/** The `ble` steps: with a baseline, the difference; without, every user service removed (tolerantly) and created. */
const bleSteps = (current: BleProfile, baseline: BleProfile | undefined): SettingsStep[] => {
  const steps: SettingsStep[] = []
  const now = userServices(current)
  const before = userServices(baseline)
  if (!baseline) {
    for (const service of now) steps.push(svcRemove(service, true))
    for (const service of now) {
      steps.push(svcCreate(service))
      for (const char of userCharacteristics(service)) steps.push(charCreate(service, char))
    }
  } else {
    for (const old of before) {
      const next = now.find((service) => number(service.uuid) === number(old.uuid))
      if (!next || next.isPrimary !== old.isPrimary) {
        steps.push(svcRemove(old))
        continue
      }
      for (const char of userCharacteristics(old)) {
        const kept = userCharacteristics(next).find((entry) => number(entry.uuid) === number(char.uuid))
        if (!kept || !sameChar(kept, char)) steps.push(charRemove(old, char))
      }
    }
    for (const service of now) {
      const old = before.find((entry) => number(entry.uuid) === number(service.uuid))
      const survived = old && old.isPrimary === service.isPrimary
      if (!survived) steps.push(svcCreate(service))
      for (const char of userCharacteristics(service)) {
        const had = survived ? userCharacteristics(old).find((entry) => number(entry.uuid) === number(char.uuid)) : undefined
        if (!had || !sameChar(had, char)) steps.push(charCreate(service, char))
      }
    }
  }
  if (steps.length) steps.push({ ...frame('Apply the GATT table', BLE, 'OP_APPLY'), reconnects: true })
  return steps
}

const bindingStep = (connector: ConnectorSettings, binding: ConnectorBindingSettings): SettingsStep =>
  frame(`Bind ${connector.alias} to ${binding.provider} ${binding.endpoint}`, CONN, 'OP_BIND', { id: connector.id, provider: transportId(binding.provider), endpoint: endpointOf(binding) })

const unbindStep = (connector: ConnectorSettings, binding: ConnectorBindingSettings): SettingsStep =>
  frame(`Unbind ${connector.alias} from ${binding.provider}`, CONN, 'OP_UNBIND', { id: connector.id, provider: transportId(binding.provider) })

const sameConnectorIdentity = (a: ConnectorSettings, b: ConnectorSettings): boolean => a.id === b.id && a.name === b.name && number(a.stream) === number(b.stream)

const removeConnector = (connector: ConnectorSettings, tolerant = false): SettingsStep =>
  frame(`Remove stream ${connector.alias}`, CONN, 'OP_REMOVE', { id: connector.id }, undefined, tolerant)

/** The connector steps that must run before the GATT table changes (a stream bound to a characteristic about to go), and those after. */
const connectorSteps = (current: readonly ConnectorSettings[], baseline: readonly ConnectorSettings[] | undefined): { before: SettingsStep[]; after: SettingsStep[] } => {
  const before: SettingsStep[] = []
  const after: SettingsStep[] = []
  const create = (connector: ConnectorSettings) => {
    after.push(frame(`Create stream ${connector.alias}`, CONN, 'OP_CREATE', { id: connector.id, stream: number(connector.stream) }, nameTail(connector.name)))
  }
  const bind = (connector: ConnectorSettings, known: readonly ConnectorBindingSettings[]) => {
    for (const binding of connector.bindings) {
      const had = known.find((entry) => entry.provider === binding.provider)
      if (!had || endpointOf(had) !== endpointOf(binding)) after.push(bindingStep(connector, binding))
    }
    if (connector.isSuspended) after.push(frame(`Suspend ${connector.alias}`, CONN, 'OP_SUSPEND', { id: connector.id }))
  }

  if (!baseline) {
    for (const connector of userConnectors(current)) before.push(removeConnector(connector, true))
    for (const connector of userConnectors(current)) {
      create(connector)
      bind(connector, [])
    }
    // The board's own streams: the defaults are what it holds, so only what differs is sent (a re-address binds again).
    return { before, after: [...after, ...systemSteps(current, undefined)] }
  }

  for (const old of userConnectors(baseline)) {
    const next = userConnectors(current).find((connector) => connector.id === old.id)
    if (!next || !sameConnectorIdentity(next, old)) before.push(removeConnector(old))
  }
  for (const connector of userConnectors(current)) {
    const old = userConnectors(baseline).find((entry) => entry.id === connector.id)
    if (!old || !sameConnectorIdentity(connector, old)) {
      create(connector)
      bind(connector, [])
      continue
    }
    for (const binding of old.bindings) if (!connector.bindings.some((entry) => entry.provider === binding.provider)) before.push(unbindStep(connector, binding))
    bind(connector, old.bindings)
    if (old.isSuspended && !connector.isSuspended) after.push(frame(`Resume ${connector.alias}`, CONN, 'OP_RESUME', { id: connector.id }))
  }
  after.push(...systemSteps(current, baseline))
  return { before, after }
}

/** The board's own streams (logs, errors, telemetry, events): never created or removed, only re-bound and paused. A board that was just started holds the defaults. */
const systemSteps = (current: readonly ConnectorSettings[], baseline: readonly ConnectorSettings[] | undefined): SettingsStep[] => {
  const steps: SettingsStep[] = []
  const held = baseline ?? boardConnectors()
  for (const connector of current.filter((entry) => entry.system)) {
    const old = held.find((entry) => entry.id === connector.id)
    const known = old?.bindings ?? []
    for (const binding of known) if (!connector.bindings.some((entry) => entry.provider === binding.provider)) steps.push(unbindStep(connector, binding))
    for (const binding of connector.bindings) {
      const had = known.find((entry) => entry.provider === binding.provider)
      if (!had || endpointOf(had) !== endpointOf(binding)) steps.push(bindingStep(connector, binding))
    }
    if ((old?.isSuspended ?? false) !== connector.isSuspended) steps.push(frame(`${connector.isSuspended ? 'Suspend' : 'Resume'} ${connector.alias}`, CONN, connector.isSuspended ? 'OP_SUSPEND' : 'OP_RESUME', { id: connector.id }))
  }
  return steps
}

const ACTIONS = 'ROUTER_KEY_ACTIONS'

/** The frames stored as one record: RECORD_START, the frames, RECORD_STOP. `execute` runs them as they are recorded (those that fail are not stored). */
const recorded = (label: string, id: number, execute: boolean, frames: readonly SettingsStep[]): SettingsStep[] => [
  frame(`Record ${label}`, ACTIONS, 'SYS_ACTIONS_OP_RECORD_START', { id, execute: flag(execute) }),
  // recorded only: nothing runs, so nothing drops the link
  ...frames.map(({ reconnects: _reconnects, ...step }): SettingsStep => ({ ...step, recorded: true })),
  frame(`Store ${label}`, ACTIONS, 'SYS_ACTIONS_OP_RECORD_STOP'),
]

/** A record with nothing to hold is removed (a refusal for a record that was never stored is ignored). */
const removeRecord = (label: string, id: number): SettingsStep => frame(`Remove ${label}`, ACTIONS, 'SYS_ACTIONS_OP_REMOVE', { id }, undefined, true)

/** The frames the records of the GATT table and the streams would hold for these settings, by record id (an empty list: the record is removed). */
export const settingsImages = (current: SettingsSnapshot): ReadonlyMap<number, readonly Uint8Array[]> => {
  const bytes = (steps: readonly SettingsStep[]) => steps.map((step) => Uint8Array.of(...step.keys, ...(step.payload ?? [])))
  const image = connectorSteps(current.connectors, boardConnectors())
  const out = new Map<number, readonly Uint8Array[]>()
  const ble = opRecord(BLE, 'OP_SVC_CREATE')
  const conn = opRecord(CONN, 'OP_CREATE')
  if (ble !== undefined) out.set(ble, bytes(bleSteps(current.ble, defaultBleProfile())))
  if (conn !== undefined) out.set(conn, bytes([...image.before, ...image.after]))
  return out
}

/**
 * Records stored as they are, not run (a device is installed when the project runs): the devices' install and default settings and the
 * actions. A record with no frames is removed, unless the board is known not to hold it (`stored`).
 */
export const planRecords = (records: readonly RecordFrames[], stored?: ReadonlySet<number>): SettingsStep[] =>
  records.flatMap((record) => {
    const frames = record.frames.map((bytes): SettingsStep => ({ label: record.label, keys: [bytes[0]!, bytes[1]!], payload: bytes.subarray(2) }))
    if (frames.length) return recorded(record.label, record.id, false, frames)
    return stored && !stored.has(record.id) ? [] : [removeRecord(record.label, record.id)]
  })

/**
 * The frames of a save, in the order the board needs them:
 *   1. every `@settings` op (log, power) recorded as its record with `execute`: applied now and kept in NVS;
 *   2. the `ble` / `conn` changes applied now (the difference from the baseline);
 *   3. the `ble` and `conn` records: the whole set as it would be built on a board that has just started (creates, binds, pauses),
 *      recorded without running, because that is what a boot replays.
 */
export const planSettings = (current: SettingsSnapshot, routers: readonly RouterValues[], baseline: Partial<SettingsSnapshot> | undefined, stored?: ReadonlySet<number>, records: readonly RecordFrames[] = []): SettingsStep[] => {
  const steps: SettingsStep[] = []
  for (const router of routers) {
    for (const op of router.ops) {
      const values = router.values[op.symbol]
      const id = opRecord(op.routerSymbol, op.symbol)
      if (!values) continue
      const step = frame(op.alias, op.routerSymbol, op.symbol, values)
      steps.push(...(id === undefined ? [step] : recorded(op.alias, id, true, [step])))
    }
  }
  const connectors = connectorSteps(current.connectors, baseline?.connectors)
  steps.push(...connectors.before, ...bleSteps(current.ble, baseline?.ble), ...connectors.after)

  const bleImage = bleSteps(current.ble, defaultBleProfile())
  const bleRecord = opRecord(BLE, 'OP_SVC_CREATE')
  // an empty record is removed, unless the board is known not to hold it (`stored`: the records it lists)
  const gone = (id: number) => (stored && !stored.has(id) ? [] : [removeRecord(id === bleRecord ? 'Bluetooth services' : 'data streams', id)])
  if (bleRecord !== undefined) steps.push(...(bleImage.length ? recorded('Bluetooth services', bleRecord, false, bleImage) : gone(bleRecord)))
  const image = connectorSteps(current.connectors, boardConnectors())
  const connImage = [...image.before, ...image.after]
  const connRecord = opRecord(CONN, 'OP_CREATE')
  if (connRecord !== undefined) steps.push(...(connImage.length ? recorded('data streams', connRecord, false, connImage) : gone(connRecord)))
  steps.push(...planRecords(records, stored))
  return steps
}
