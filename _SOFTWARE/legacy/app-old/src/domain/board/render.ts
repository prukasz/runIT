import type { NetSpec, PinDir, PinSpec } from '../hardware'
import { CONNECTOR_STYLE, connectorType } from './connectors'
import type { ConnectorType } from './connectors'
import { terminalId } from './template'
import type { BoardConnector, BoardTemplate, PinBinding, SysPinRef } from './template'

/** The art is drawn at 20 units to the millimetre, like the built-in board art. */
export const UNITS_PER_MM = 20

export interface Point {
  readonly x: number
  readonly y: number
}

// ---- Geometry: where a connector and its pins are on the board, in millimetres ----------------------------------

/** A point of the connector (millimetres from its centre, before rotation) on the board: mirror, rotate, then move. */
export function onBoard(connector: BoardConnector, local: Point): Point {
  const x = connector.mirror ? -local.x : local.x
  const angle = (connector.rotation * Math.PI) / 180
  const cos = Math.round(Math.cos(angle))
  const sin = Math.round(Math.sin(angle))
  return { x: connector.x + x * cos - local.y * sin, y: connector.y + x * sin + local.y * cos }
}

/** The inverse: a board point as the connector sees it. */
export function inConnector(connector: BoardConnector, point: Point): Point {
  const dx = point.x - connector.x
  const dy = point.y - connector.y
  const angle = (-connector.rotation * Math.PI) / 180
  const cos = Math.round(Math.cos(angle))
  const sin = Math.round(Math.sin(angle))
  const x = dx * cos - dy * sin
  return { x: connector.mirror ? -x : x, y: dx * sin + dy * cos }
}

export function connectorCorners(connector: BoardConnector): Point[] {
  const type = connectorType(connector.type)
  if (!type) return []
  const hw = type.width / 2
  const hh = type.height / 2
  return [{ x: -hw, y: -hh }, { x: hw, y: -hh }, { x: hw, y: hh }, { x: -hw, y: hh }].map((corner) => onBoard(connector, corner))
}

export function connectorBounds(connector: BoardConnector): { left: number; top: number; right: number; bottom: number } {
  const corners = connectorCorners(connector)
  const xs = corners.map((corner) => corner.x)
  const ys = corners.map((corner) => corner.y)
  return { left: Math.min(...xs), top: Math.min(...ys), right: Math.max(...xs), bottom: Math.max(...ys) }
}

export function pointInConnector(connector: BoardConnector, point: Point, margin = 0): boolean {
  const type = connectorType(connector.type)
  if (!type) return false
  const local = inConnector(connector, point)
  return Math.abs(local.x) <= type.width / 2 + margin && Math.abs(local.y) <= type.height / 2 + margin
}

/** Where pin `index` of a connector is on the board. */
export function pinPosition(connector: BoardConnector, index: number): Point | undefined {
  const shape = connectorType(connector.type)?.pins[index]
  return shape ? onBoard(connector, shape) : undefined
}

/** The pin of a connector within `reach` millimetres of a board point, if any. */
export function pinAt(connector: BoardConnector, point: Point, reach: number): number | undefined {
  const type = connectorType(connector.type)
  let best: { index: number; distance: number } | undefined
  type?.pins.forEach((_, index) => {
    const at = pinPosition(connector, index)
    const distance = at ? Math.hypot(at.x - point.x, at.y - point.y) : Infinity
    if (distance <= reach && (!best || distance < best.distance)) best = { index, distance }
  })
  return best?.index
}

/**
 * The free spot nearest the board's centre for a new connector of this type (rotation 0): inside the board with a margin
 * and clear of every other connector. On a full board it is the centre.
 */
export function freeSpot(board: BoardTemplate, type: ConnectorType, snap = 0.5, margin = 1): Point {
  const halfW = type.width / 2
  const halfH = type.height / 2
  const centre = { x: board.width / 2, y: board.height / 2 }
  const round = (value: number): number => (snap ? Math.round(value / snap) * snap : value)
  const taken = board.connectors.map(connectorBounds)
  const clear = (x: number, y: number): boolean =>
    x - halfW >= margin && x + halfW <= board.width - margin && y - halfH >= margin && y + halfH <= board.height - margin &&
    taken.every((box) => x - halfW >= box.right + margin || x + halfW <= box.left - margin || y - halfH >= box.bottom + margin || y + halfH <= box.top - margin)
  const spots: Point[] = [{ x: round(centre.x), y: round(centre.y) }]
  const step = 1
  for (let y = halfH + margin; y <= board.height - halfH - margin; y += step) for (let x = halfW + margin; x <= board.width - halfW - margin; x += step) spots.push({ x: round(x), y: round(y) })
  spots.sort((a, b) => Math.hypot(a.x - centre.x, a.y - centre.y) - Math.hypot(b.x - centre.x, b.y - centre.y))
  return spots.find((spot) => clear(spot.x, spot.y)) ?? { x: round(centre.x), y: round(centre.y) }
}

// ---- Art ---------------------------------------------------------------------------------------------------------

const escapeXml = (text: string): string => text.replace(/[&<>"]/g, (char) => `&#${char.charCodeAt(0)};`)

export const pinLabel = (connector: BoardConnector, index: number): string => connector.pins[index]?.label?.trim() || connectorType(connector.type)?.pins[index]?.name || String(index + 1)

/**
 * The board as SVG (millimetres scaled by 20): the outline, the mount holes and every connector with a terminal group
 * `<id>-<n>` per pin. `describe` writes each pin's tooltip. The connector wrapper carries no id, so the pin group is the
 * outermost id'd group, which is what the canvas takes as the terminal.
 */
export function renderBoardSvg(board: BoardTemplate, describe: (connector: BoardConnector, index: number) => string = (connector, index) => pinLabel(connector, index)): string {
  const parts: string[] = [`<rect class="pcb" x="0" y="0" width="${board.width}" height="${board.height}" rx="${board.radius}"/>`]
  for (const hole of board.holes) parts.push(`<circle class="hole-ring" cx="${hole.x}" cy="${hole.y}" r="${board.holeDiameter / 2 + 0.7}"/><circle class="hole-bore" cx="${hole.x}" cy="${hole.y}" r="${board.holeDiameter / 2}"/>`)

  for (const connector of board.connectors) {
    const type = connectorType(connector.type)
    if (!type) continue
    const shapes = type.draw((index, shape) => `<g id="${escapeXml(terminalId(connector.id, index + 1))}"><title>${escapeXml(describe(connector, index))}</title>${shape}</g>`)
    const mirror = connector.mirror ? -1 : 1
    const labels = type.pins.map((pin, index) => {
      const text = pinLabel(connector, index)
      const y = pin.y >= 0 ? type.height / 2 + 1.3 : -type.height / 2 - 0.5
      // The text stays upright: the inverse of the connector's mirror and rotation.
      return `<text class="pin-label" font-size="${type.labelSize}" transform="translate(${pin.x} ${y}) scale(${mirror} 1) rotate(${-connector.rotation})">${escapeXml(text)}</text>`
    })
    parts.push(`<g data-connector="${escapeXml(connector.id)}" transform="translate(${connector.x} ${connector.y}) rotate(${connector.rotation}) scale(${mirror} 1)">${shapes}${labels.join('')}</g>`)
  }
  const width = board.width * UNITS_PER_MM
  const height = board.height * UNITS_PER_MM
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${width} ${height}"><style>\n${CONNECTOR_STYLE}\n</style><g transform="scale(${UNITS_PER_MM})">${parts.join('')}</g></svg>`
}

// ---- What the pins are electrically ---------------------------------------------------------------------------------

/** A bus signal a pin can carry: its ID (`i2c-sda`), the name shown, and which way it flows at the pin. */
export interface SignalSpec {
  readonly id: string
  readonly label: string
  readonly dir: PinDir
}

export interface SpecContext {
  readonly nets: Readonly<Record<string, NetSpec>>
  readonly signals: readonly SignalSpec[]
  /** The electrical meaning of an installed sys_io pin (what the pin's device provides); undefined when it is not a known pin. */
  readonly sysPin: (ref: SysPinRef) => PinSpec | undefined
}

/** What a binding makes the pin: undefined for an unbound pin or one that cannot be resolved. */
export function bindingSpec(binding: PinBinding, label: string, context: SpecContext): PinSpec | undefined {
  switch (binding.type) {
    case 'none':
      return undefined
    case 'net': {
      const net = context.nets[binding.net]
      if (!net) return undefined
      if (net.kind === 'gnd') return { kind: 'gnd', dir: 'in', label, modes: [], net: binding.net }
      return binding.role === 'input'
        ? { kind: 'supply_in', dir: 'in', label, modes: [], net: binding.net, ...(net.note ? { note: net.note } : {}) }
        : { kind: 'supply_out', dir: 'out', label, modes: [], net: binding.net, ...(net.note ? { note: net.note } : {}) }
    }
    case 'signal': {
      const signal = context.signals.find((entry) => entry.id === binding.signal)
      if (!signal) return undefined
      return { kind: 'bus', dir: signal.dir, label, modes: [], signal: signal.id, ...(binding.sys ? { sysPin: binding.sys } : {}) }
    }
    case 'sys': {
      const base = context.sysPin({ device: binding.device, pin: binding.pin })
      if (!base) return undefined
      const o = binding.overrides
      return {
        ...base,
        label,
        dir: o?.dir ?? base.dir,
        modes: o?.modes ?? base.modes,
        ...(o?.mV_min !== undefined ? { mV_min: o.mV_min } : {}),
        ...(o?.mV_max !== undefined ? { mV_max: o.mV_max } : {}),
        sysPin: { device: binding.device, pin: binding.pin },
      }
    }
  }
}

/** The wirable terminals of a board: every bound connector pin, by terminal ID. */
export function boardTerminals(board: BoardTemplate, context: SpecContext): Map<string, PinSpec> {
  const terminals = new Map<string, PinSpec>()
  for (const connector of board.connectors) {
    if (!connectorType(connector.type)) continue
    connector.pins.forEach((pin, index) => {
      const spec = bindingSpec(pin.bind, pinLabel(connector, index), context)
      if (spec) terminals.set(terminalId(connector.id, index + 1), spec)
    })
  }
  return terminals
}

export type { ConnectorType }
