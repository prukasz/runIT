import { bundledEnums } from '@backend'
import type { LinkDefinitions, LiveGattService, SessionPeer } from '@backend'

/*
 * Pure model for the board's Providers, Sessions and Data Matrix (DATA_MATRIX.MD, SYS_DATA_PROVIDER.MD, SYS_SESSION.MD).
 * Every enum value and module number is read from `generated/common/enums.json` via `bundledEnums()` / `LinkDefinitions`.
 */

const enumValue = (group: string, symbol: string): number => {
  const member = bundledEnums()[group]?.members.find((m) => m.name === symbol)
  if (!member) throw new Error(`enums.json is missing ${group}.${symbol}`)
  return member.value
}

export const PORT_KIND = {
  module: enumValue('data_matrix_port_kind_e', 'DATA_MATRIX_PORT_MODULE'),
  provider: enumValue('data_matrix_port_kind_e', 'DATA_MATRIX_PORT_PROVIDER'),
  session: enumValue('data_matrix_port_kind_e', 'DATA_MATRIX_PORT_SESSION'),
} as const

export const PROV_STATE = {
  system: enumValue('prov_state', 'SYS_DATA_PROVIDER_STATE_SYSTEM'),
  wired: enumValue('prov_state', 'SYS_DATA_PROVIDER_STATE_WIRED'),
  up: enumValue('prov_state', 'SYS_DATA_PROVIDER_STATE_UP'),
  framed: enumValue('prov_state', 'SYS_DATA_PROVIDER_STATE_FRAMED'),
} as const

export const PROV_TYPE = {
  uart: enumValue('sys_data_provider_type_e', 'SYS_DATA_PROVIDER_TYPE_UART'),
  ble: enumValue('sys_data_provider_type_e', 'SYS_DATA_PROVIDER_TYPE_BLE'),
  mqtt: enumValue('sys_data_provider_type_e', 'SYS_DATA_PROVIDER_TYPE_MQTT'),
  console: enumValue('sys_data_provider_type_e', 'SYS_DATA_PROVIDER_TYPE_CONSOLE'),
} as const

export const SESSION_SAFETY_FLAG = enumValue('session_flag', 'SYS_SESSION_SAFETY')
export const CMD_SEND_NOWAIT = enumValue('cmd_send_flag', 'CMD_SEND_NOWAIT')

/** Encodes a data_matrix_port_id_t (`DATA_MATRIX_PORT(kind, id)`). */
export const makePort = (kind: number, id: number): number => (((kind & 0xff) << 8) | (id & 0xff)) >>> 0
export const modulePort = (moduleId: number): number => makePort(PORT_KIND.module, moduleId)
export const providerPort = (providerId: number): number => makePort(PORT_KIND.provider, providerId)
export const sessionPort = (providerId: number, moduleId = 0): number => makePort(PORT_KIND.session | (providerId & 0x7f), moduleId)

export type ParsedPort =
  | { readonly kind: 'module'; readonly moduleId: number }
  | { readonly kind: 'provider'; readonly providerId: number }
  | { readonly kind: 'session'; readonly providerId: number; readonly moduleId: number }
  | { readonly kind: 'unknown'; readonly hi: number; readonly lo: number }

export const parsePort = (port: number): ParsedPort => {
  const hi = (port >>> 8) & 0xff
  const lo = port & 0xff
  if (hi === PORT_KIND.module) return { kind: 'module', moduleId: lo }
  if (hi === PORT_KIND.provider) return { kind: 'provider', providerId: lo }
  if ((hi & PORT_KIND.session) !== 0) return { kind: 'session', providerId: hi & 0x7f, moduleId: lo }
  return { kind: 'unknown', hi, lo }
}

/** One link returned by DATA_MATRIX LIST (`data_matrix_link_t`: from u16 LE, to u16 LE). */
export interface MatrixLink {
  readonly from: number
  readonly to: number
}

/** Decodes DATA_MATRIX LIST raw reply: `[count u8][from u16 LE, to u16 LE] * count`. */
export const decodeMatrixLinks = (data: Uint8Array): MatrixLink[] => {
  if (data.length < 1) return []
  const count = data[0]!
  const out: MatrixLink[] = []
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
  for (let i = 0; i < count && 1 + (i + 1) * 4 <= data.length; i++) {
    const off = 1 + i * 4
    out.push({ from: view.getUint16(off, true), to: view.getUint16(off + 2, true) })
  }
  return out
}

export interface LiveProvider {
  readonly id: number
  readonly type: number
  readonly typeLabel: string
  /** The project's name of it, when the user gave one. */
  readonly name?: string
  readonly state: number
  readonly system: boolean
  readonly wired: boolean
  readonly up: boolean
  readonly framed: boolean
}

/** How a provider is called: the user's name with its id, else its id and type. */
export const providerTitle = (p: Pick<LiveProvider, 'id' | 'typeLabel' | 'name'>): string => (p.name ? `${p.name} (#${p.id})` : `Provider #${p.id} (${p.typeLabel})`)

/** The snapshot with the project's names on its providers. */
export const withProviderNames = (snapshot: RoutingSnapshot, names: Readonly<Record<string, string>> | undefined): RoutingSnapshot =>
  names ? { ...snapshot, providers: snapshot.providers.map((p) => (names[p.id]?.trim() ? { ...p, name: names[p.id]!.trim() } : p)) } : snapshot

export const providerTypeLabel = (type: number): string => {
  const m = bundledEnums().sys_data_provider_type_e?.members.find((x) => x.value === type)
  return m?.alias ?? m?.name ?? `Type ${type}`
}

/** Decodes PROVIDER LIST raw reply: `[count u8][provider u8, type u8, state u8] * count`. */
export const decodeProviderRows = (data: Uint8Array): LiveProvider[] => {
  if (data.length < 1) return []
  const count = data[0]!
  const rows: LiveProvider[] = []
  for (let i = 0; i < count && 1 + (i + 1) * 3 <= data.length; i++) {
    const off = 1 + i * 3
    const id = data[off]!
    const type = data[off + 1]!
    const state = data[off + 2]!
    rows.push({
      id,
      type,
      typeLabel: providerTypeLabel(type),
      state,
      system: (state & PROV_STATE.system) !== 0,
      wired: (state & PROV_STATE.wired) !== 0,
      up: (state & PROV_STATE.up) !== 0,
      framed: (state & PROV_STATE.framed) !== 0,
    })
  }
  return rows.sort((a, b) => a.id - b.id)
}

export interface ModulePortSpec {
  readonly moduleId: number
  readonly name: string
  readonly short: string
  readonly description: string
  readonly canIn: boolean
  readonly canOut: boolean
}

/** The firmware's standard module ports registered in the data matrix (RUNIT.MD "The board's data"). */
export const standardModulePorts = (defs: LinkDefinitions): ModulePortSpec[] => [
  { moduleId: defs.keys.router!, name: 'Router', short: 'ROUTER', description: 'Incoming commands to the board router', canIn: true, canOut: false },
  { moduleId: defs.keys.errors!, name: 'Errors', short: 'ERRORS', description: 'Error records (outbound + relayed from other boards)', canIn: true, canOut: true },
  { moduleId: defs.keys.sys_event!, name: 'Events', short: 'EVENTS', description: 'Notify events stream (bidirectional across boards)', canIn: true, canOut: true },
  { moduleId: defs.keys.watch!, name: 'Watch', short: 'WATCH', description: 'Periodic / on-change watch telemetry entries', canIn: true, canOut: true },
  { moduleId: defs.keys.log!, name: 'Logs', short: 'LOGS', description: 'Captured ESP_LOG text lines (output only)', canIn: false, canOut: true },
  { moduleId: defs.keys.remote!, name: 'Remote', short: 'REMOTE', description: 'Cross-board remote device frames', canIn: true, canOut: true },
]

export interface RoutingSnapshot {
  readonly providers: readonly LiveProvider[]
  readonly sessions: readonly SessionPeer[]
  readonly links: readonly MatrixLink[]
  readonly gatt: readonly LiveGattService[]
}

/** A realistic preview of the board's default boot setup when offline (RUNIT.MD / data_matrix_setup.c). */
export const offlinePreviewSnapshot = (defs: LinkDefinitions): RoutingSnapshot => {
  const m = defs.keys
  const consoleProv: LiveProvider = { id: 1, type: PROV_TYPE.console, typeLabel: providerTypeLabel(PROV_TYPE.console), state: PROV_STATE.system | PROV_STATE.wired | PROV_STATE.up | PROV_STATE.framed, system: true, wired: true, up: true, framed: true }
  const bleProv: LiveProvider = { id: 3, type: PROV_TYPE.ble, typeLabel: providerTypeLabel(PROV_TYPE.ble), state: PROV_STATE.system | PROV_STATE.framed, system: true, wired: false, up: false, framed: true }
  const bleLogs: LiveProvider = { id: 4, type: PROV_TYPE.ble, typeLabel: providerTypeLabel(PROV_TYPE.ble), state: PROV_STATE.system, system: true, wired: false, up: false, framed: false }
  const bleTelem: LiveProvider = { id: 5, type: PROV_TYPE.ble, typeLabel: providerTypeLabel(PROV_TYPE.ble), state: PROV_STATE.system, system: true, wired: false, up: false, framed: false }
  const links: MatrixLink[] = [
    // Console session <-> provider
    { from: providerPort(1), to: sessionPort(1, 0) },
    { from: sessionPort(1, 0), to: providerPort(1) },
    { from: sessionPort(1, m.router!), to: modulePort(m.router!) },
    { from: sessionPort(1, m.errors!), to: modulePort(m.errors!) },
    { from: modulePort(m.errors!), to: sessionPort(1, m.errors!) },
    { from: sessionPort(1, m.sys_event!), to: modulePort(m.sys_event!) },
    { from: modulePort(m.sys_event!), to: sessionPort(1, m.sys_event!) },
    { from: sessionPort(1, m.watch!), to: modulePort(m.watch!) },
    { from: modulePort(m.watch!), to: sessionPort(1, m.watch!) },
    { from: modulePort(m.log!), to: sessionPort(1, m.log!) },
    { from: sessionPort(1, m.remote!), to: modulePort(m.remote!) },
    // BLE session <-> provider
    { from: providerPort(3), to: sessionPort(3, 0) },
    { from: sessionPort(3, 0), to: providerPort(3) },
    { from: sessionPort(3, m.router!), to: modulePort(m.router!) },
    { from: sessionPort(3, m.errors!), to: modulePort(m.errors!) },
    { from: modulePort(m.errors!), to: sessionPort(3, m.errors!) },
    { from: sessionPort(3, m.sys_event!), to: modulePort(m.sys_event!) },
    { from: modulePort(m.sys_event!), to: sessionPort(3, m.sys_event!) },
    { from: sessionPort(3, m.remote!), to: modulePort(m.remote!) },
    // Direct raw BLE streams
    { from: modulePort(m.log!), to: providerPort(4) },
    { from: modulePort(m.watch!), to: providerPort(5) },
  ]
  return {
    providers: [consoleProv, bleProv, bleLogs, bleTelem],
    sessions: [
      { provider: 1, open: false, safety: false, buildId: 0, deadmanMs: 0 },
      { provider: 3, open: false, safety: false, buildId: 0, deadmanMs: 0 },
    ],
    links,
    gatt: [],
  }
}
