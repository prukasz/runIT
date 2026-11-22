import { bundledEnums } from '@backend'
import type { LinkDefinitions, LiveGattService, SessionPeer } from '@backend'

/*
 * Pure model for the board's Providers, Sessions and Data Matrix graph (DATA_MATRIX.MD, SYS_DATA_PROVIDER.MD, SYS_SESSION.MD).
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
  readonly state: number
  readonly system: boolean
  readonly wired: boolean
  readonly up: boolean
  readonly framed: boolean
}

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

export const moduleNameOf = (defs: LinkDefinitions, moduleId: number): string => {
  const std = standardModulePorts(defs).find((m) => m.moduleId === moduleId)
  if (std) return std.name
  const entry = Object.entries(defs.keys).find(([, v]) => v === moduleId)
  return entry ? entry[0].toUpperCase() : `0x${moduleId.toString(16).toUpperCase().padStart(2, '0')}`
}

export const describePort = (port: number, providers: readonly LiveProvider[], defs: LinkDefinitions): string => {
  const p = parsePort(port)
  if (p.kind === 'module') return `Module · ${moduleNameOf(defs, p.moduleId)}`
  if (p.kind === 'provider') {
    const prov = providers.find((x) => x.id === p.providerId)
    return prov ? `Provider #${prov.id} (${prov.typeLabel})` : `Provider #${p.providerId}`
  }
  if (p.kind === 'session') {
    if (p.moduleId === 0) return `Session #${p.providerId} (line)`
    return `Session #${p.providerId} · ${moduleNameOf(defs, p.moduleId)}`
  }
  return `0x${port.toString(16).toUpperCase().padStart(4, '0')}`
}

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

/* ---- Graph layout (3 columns: Modules | Sessions | Providers) ---- */

export interface PortPin {
  readonly portId: number
  readonly label: string
  readonly x: number
  readonly y: number
  readonly canIn: boolean
  readonly canOut: boolean
  readonly connected: boolean
}

export interface ModuleNode {
  readonly spec: ModulePortSpec
  readonly portId: number
  readonly x: number
  readonly y: number
  readonly w: number
  readonly h: number
  readonly connected: boolean
  readonly inPin?: { readonly x: number; readonly y: number }
  readonly outPin?: { readonly x: number; readonly y: number }
}

export interface SessionSubRow {
  readonly moduleId: number
  readonly portId: number
  readonly label: string
  readonly y: number
  readonly inPin: { readonly x: number; readonly y: number }
  readonly outPin: { readonly x: number; readonly y: number }
  readonly connected: boolean
}

export interface SessionNode {
  readonly session: SessionPeer
  readonly linePortId: number
  readonly x: number
  readonly y: number
  readonly w: number
  readonly h: number
  readonly linePin: { readonly x: number; readonly y: number }
  readonly subRows: readonly SessionSubRow[]
}

export interface ProviderNode {
  readonly provider: LiveProvider
  readonly portId: number
  readonly x: number
  readonly y: number
  readonly w: number
  readonly h: number
  readonly connected: boolean
  readonly hasSession: boolean
  readonly inPin: { readonly x: number; readonly y: number }
  readonly outPin: { readonly x: number; readonly y: number }
}

export type EdgeKind = 'module-session' | 'session-provider' | 'direct' | 'bridge'

export interface GraphEdge {
  readonly id: string
  readonly from: number
  readonly to: number
  readonly x1: number
  readonly y1: number
  readonly x2: number
  readonly y2: number
  readonly kind: EdgeKind
  readonly bidirectional: boolean
}

export interface GraphLayout {
  readonly width: number
  readonly height: number
  readonly modules: readonly ModuleNode[]
  readonly sessions: readonly SessionNode[]
  readonly providers: readonly ProviderNode[]
  readonly edges: readonly GraphEdge[]
  readonly pins: ReadonlyMap<number, { readonly in?: { x: number; y: number }; readonly out?: { x: number; y: number }; readonly label: string; readonly nodeKey: string }>
}

const COL_MOD_X = 36
const COL_SES_X = 370
const COL_PRV_X = 760
const MOD_W = 200
const MOD_H = 62
const SES_W = 250
const SES_HEAD_H = 54
const SES_ROW_H = 26
const PRV_W = 264
const PRV_H = 88
const GAP_Y = 18

export const buildGraphLayout = (snapshot: RoutingSnapshot, defs: LinkDefinitions): GraphLayout => {
  const touched = new Set<number>()
  for (const l of snapshot.links) {
    touched.add(l.from)
    touched.add(l.to)
  }

  // 1. Modules column: standard ports + any extra module port referenced by links
  const std = standardModulePorts(defs)
  const modSpecs: ModulePortSpec[] = [...std]
  for (const p of touched) {
    const parsed = parsePort(p)
    if (parsed.kind === 'module' && !modSpecs.some((s) => s.moduleId === parsed.moduleId)) {
      modSpecs.push({ moduleId: parsed.moduleId, name: moduleNameOf(defs, parsed.moduleId), short: `0x${parsed.moduleId.toString(16).toUpperCase()}`, description: 'Module port', canIn: true, canOut: true })
    }
  }

  const pins = new Map<number, { in?: { x: number; y: number }; out?: { x: number; y: number }; label: string; nodeKey: string }>()

  let modY = 56
  const modules: ModuleNode[] = modSpecs.map((spec) => {
    const portId = modulePort(spec.moduleId)
    const y = modY
    modY += MOD_H + GAP_Y
    const inPin = spec.canIn ? { x: COL_MOD_X + MOD_W, y: y + (spec.canOut ? 22 : MOD_H / 2) } : undefined
    const outPin = spec.canOut ? { x: COL_MOD_X + MOD_W, y: y + (spec.canIn ? 42 : MOD_H / 2) } : undefined
    pins.set(portId, { ...(inPin ? { in: inPin } : {}), ...(outPin ? { out: outPin } : {}), label: `Module · ${spec.name}`, nodeKey: `mod:${spec.moduleId}` })
    return { spec, portId, x: COL_MOD_X, y, w: MOD_W, h: MOD_H, connected: touched.has(portId), ...(inPin ? { inPin } : {}), ...(outPin ? { outPin } : {}) }
  })

  // 2. Sessions column: each session from SESSION PEERS, with its sub-ports discovered from links
  const sessionProviders = new Set(snapshot.sessions.map((s) => s.provider))
  let sesY = 56
  const sessions: SessionNode[] = snapshot.sessions.map((session) => {
    const subSet = new Set<number>()
    for (const p of touched) {
      const parsed = parsePort(p)
      if (parsed.kind === 'session' && parsed.providerId === session.provider && parsed.moduleId !== 0) {
        subSet.add(parsed.moduleId)
      }
    }
    const orderedSubs = [...subSet].sort((a, b) => {
      const ia = std.findIndex((s) => s.moduleId === a)
      const ib = std.findIndex((s) => s.moduleId === b)
      return (ia === -1 ? 999 : ia) - (ib === -1 ? 999 : ib) || a - b
    })
    const y = sesY
    const h = SES_HEAD_H + Math.max(1, orderedSubs.length) * SES_ROW_H + 12
    sesY += h + GAP_Y + 6
    const linePortId = sessionPort(session.provider, 0)
    const linePin = { x: COL_SES_X + SES_W, y: y + 28 }
    pins.set(linePortId, { in: linePin, out: linePin, label: `Session #${session.provider} (line)`, nodeKey: `ses:${session.provider}` })

    const subRows: SessionSubRow[] = orderedSubs.map((moduleId, idx) => {
      const ry = y + SES_HEAD_H + idx * SES_ROW_H + SES_ROW_H / 2
      const portId = sessionPort(session.provider, moduleId)
      const outPin = { x: COL_SES_X, y: ry - 5 }
      const inPin = { x: COL_SES_X, y: ry + 5 }
      pins.set(portId, { in: inPin, out: outPin, label: `Session #${session.provider} · ${moduleNameOf(defs, moduleId)}`, nodeKey: `ses:${session.provider}` })
      return { moduleId, portId, label: moduleNameOf(defs, moduleId), y: ry, inPin, outPin, connected: touched.has(portId) }
    })
    return { session, linePortId, x: COL_SES_X, y, w: SES_W, h, linePin, subRows }
  })

  // 3. Providers column
  let prvY = 56
  const providers: ProviderNode[] = snapshot.providers.map((provider) => {
    const portId = providerPort(provider.id)
    const y = prvY
    prvY += PRV_H + GAP_Y
    const inPin = { x: COL_PRV_X, y: y + 30 }
    const outPin = { x: COL_PRV_X, y: y + 58 }
    pins.set(portId, { in: inPin, out: outPin, label: `Provider #${provider.id} (${provider.typeLabel})`, nodeKey: `prv:${provider.id}` })
    return { provider, portId, x: COL_PRV_X, y, w: PRV_W, h: PRV_H, connected: touched.has(portId), hasSession: sessionProviders.has(provider.id), inPin, outPin }
  })

  // 4. Edges
  const pairSet = new Set(snapshot.links.map((l) => `${l.from}->${l.to}`))
  const edges: GraphEdge[] = []
  for (const link of snapshot.links) {
    const pFrom = pins.get(link.from)
    const pTo = pins.get(link.to)
    const a = pFrom?.out ?? pFrom?.in
    const b = pTo?.in ?? pTo?.out
    if (!a || !b) continue
    const pf = parsePort(link.from)
    const pt = parsePort(link.to)
    const kind: EdgeKind =
      pf.kind === 'provider' && pt.kind === 'provider' ? 'bridge'
      : (pf.kind === 'session' && pt.kind === 'provider') || (pf.kind === 'provider' && pt.kind === 'session') ? 'session-provider'
      : (pf.kind === 'module' && pt.kind === 'provider') || (pf.kind === 'provider' && pt.kind === 'module') ? 'direct'
      : 'module-session'
    edges.push({
      id: `${link.from}->${link.to}`,
      from: link.from,
      to: link.to,
      x1: a.x,
      y1: a.y,
      x2: b.x,
      y2: b.y,
      kind,
      bidirectional: pairSet.has(`${link.to}->${link.from}`),
    })
  }

  return {
    width: COL_PRV_X + PRV_W + 48,
    height: Math.max(modY, sesY, prvY, 460) + 32,
    modules,
    sessions,
    providers,
    edges,
    pins,
  }
}

/** Smooth cubic Bezier path between two handles, curving cleanly even for same-column bridges. */
export const wirePath = (x1: number, y1: number, x2: number, y2: number, sameSide = false): string => {
  if (sameSide || Math.abs(x2 - x1) < 24) {
    const bulge = Math.max(64, Math.min(140, Math.abs(y2 - y1) * 0.45 + 48))
    const dir = x1 > 500 ? -1 : 1
    return `M ${x1} ${y1} C ${x1 + dir * bulge} ${y1}, ${x2 + dir * bulge} ${y2}, ${x2} ${y2}`
  }
  const dx = Math.max(48, Math.abs(x2 - x1) * 0.45)
  const dir1 = x2 >= x1 ? 1 : -1
  return `M ${x1} ${y1} C ${x1 + dir1 * dx} ${y1}, ${x2 - dir1 * dx} ${y2}, ${x2} ${y2}`
}
