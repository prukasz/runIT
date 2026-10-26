import type { BoardDescriptor, CatalogEntry, WireField } from '@backend'
import type { DeviceInfo } from '../devices/devices'
import { devicePins, isReserved } from '../devices/devices'
import { connectorPinPoints, layLead, placedPins, takesWires, toCanvas, toPart, turnBy } from './geometry'
import type { DrawnConnector } from './geometry'
import type { Connector, Library, LibraryPart, PartPin, Point } from './library'
import { boardPartFor, partPins } from './library'
import { BOARD_ITEM, endKey } from './layout'
import type { HardwareLayout, PinEnd } from './layout'

/*
 * The canvas's nets, pure: every pin of every placed part, what it is (ground, power, a device's pin that something can use, a
 * config pin of the part's own device, a bus line, an H-bridge output, a motor lead) and which pins are joined (by a wire, or
 * inside a part: the pins of one power net, the grounds).
 */

export interface HardwareContext {
  readonly library: Library
  readonly board: BoardDescriptor
  /** The Devices view's devices: the board's and the project's. */
  readonly devices: readonly DeviceInfo[]
}

export type PinKind = 'gnd' | 'power' | 'provider' | 'consumer' | 'bus' | 'hbridge' | 'motor' | 'nc' | 'none'

export const pinKind = (pin: PartPin): PinKind =>
  pin.gnd ? 'gnd' : pin.net !== undefined ? 'power' : pin.device_pin ? 'provider' : pin.config !== undefined ? 'consumer'
    : pin.bus ? 'bus' : pin.hbridge ? 'hbridge' : pin.motor ? 'motor' : pin.nc ? 'nc' : 'none'

/** A pin of a placed part. */
export interface PinNode {
  /** `item:connector.pin` */
  readonly key: string
  readonly end: PinEnd
  readonly part: LibraryPart
  readonly connector: Connector
  readonly pin: PartPin
  readonly kind: PinKind
  /** Where it is on the canvas (mm). */
  readonly at: Point
  /** Which way a wire leaves it (a unit vector on the canvas): a laid lead's plug, along the lead; else none (a wire sags). */
  readonly exit?: Point
  /** A provider: the device whose pin it is (resolved from 'self'); a consumer, a bus pin or a motor lead: the item's own device. */
  readonly device?: number
}

/** A placed part: an item of the layout (or the board) with its part. */
export interface PlacedPart {
  readonly id: string
  readonly part: LibraryPart
  readonly x: number
  readonly y: number
  readonly rotation: 0 | 90 | 180 | 270
  readonly device?: number
}

export interface Netlist {
  readonly parts: readonly PlacedPart[]
  readonly nodes: ReadonlyMap<string, PinNode>
  /** The group (its first key) of every pin. */
  readonly group: ReadonlyMap<string, string>
  /** The pins of each group. */
  readonly members: ReadonlyMap<string, readonly string[]>
  /** The pins each wire touches, by pin key. */
  readonly wired: ReadonlyMap<string, readonly string[]>
  /** The wire leads laid out towards what they are wired to, by `item:connector` (the others are drawn as the library has them). */
  readonly leads: ReadonlyMap<string, DrawnConnector>
}

/** The parts on the canvas: the board's drawing (when the library has one) and every item whose part is known. */
export const placedParts = (layout: HardwareLayout, ctx: HardwareContext): PlacedPart[] => {
  const board = boardPartFor(ctx.library, ctx.board)
  const items = layout.items.flatMap((item): PlacedPart[] => {
    const part = ctx.library.get(item.part)
    return part ? [{ ...item, part }] : []
  })
  return board ? [{ id: BOARD_ITEM, part: board, ...layout.board }, ...items] : items
}

export const buildNetlist = (layout: HardwareLayout, ctx: HardwareContext): Netlist => {
  const parts = placedParts(layout, ctx)
  const nodes = new Map<string, PinNode>()
  for (const placed of parts) {
    const points = placedPins(placed.part, placed)
    for (const { key, connector, pin } of partPins(placed.part)) {
      const end = { item: placed.id, pin: key }
      const kind = pinKind(pin)
      const own = placed.id === BOARD_ITEM ? undefined : placed.device
      const device = pin.device_pin ? (pin.device_pin.device === 'self' ? own : pin.device_pin.device) : kind === 'consumer' || kind === 'bus' || kind === 'motor' ? own : undefined
      nodes.set(endKey(end), { key: endKey(end), end, part: placed.part, connector, pin, kind, at: points.get(key)!, ...(device !== undefined ? { device } : {}) })
    }
  }
  const parent = new Map<string, string>([...nodes.keys()].map((k) => [k, k]))
  const find = (k: string): string => {
    let r = k
    while (parent.get(r) !== r) r = parent.get(r)!
    parent.set(k, r)
    return r
  }
  const join = (a: string, b: string) => {
    if (!parent.has(a) || !parent.has(b)) return
    const [ra, rb] = [find(a), find(b)]
    if (ra !== rb) parent.set(rb, ra)
  }
  // Inside a part: one power net is one node, and so are its grounds.
  for (const placed of parts) {
    const first = new Map<string, string>()
    for (const { key, pin } of partPins(placed.part)) {
      const net = pin.gnd ? '#gnd' : pin.net
      if (net === undefined) continue
      const k = endKey({ item: placed.id, pin: key })
      const seen = first.get(net)
      if (seen) join(seen, k)
      else first.set(net, k)
    }
  }
  const wired = new Map<string, string[]>()
  for (const w of layout.wires) {
    const [a, b] = [endKey(w.a), endKey(w.b)]
    join(a, b)
    wired.set(a, [...(wired.get(a) ?? []), b])
    wired.set(b, [...(wired.get(b) ?? []), a])
  }
  const group = new Map<string, string>()
  const members = new Map<string, string[]>()
  for (const k of nodes.keys()) {
    const r = find(k)
    group.set(k, r)
    members.set(r, [...(members.get(r) ?? []), k])
  }
  // A wire lead goes where it is wired: its plug towards the middle of the pins on other parts its wires go to (where those are
  // as the library has them), and its pins with it.
  const leads = new Map<string, DrawnConnector>()
  for (const placed of parts) {
    for (const c of placed.part.connectors) {
      if (c.type !== 'wire' || !c.from) continue
      // Each pin's wires to other parts: where they go.
      const ends = c.pins.map((pin) => (wired.get(endKey({ item: placed.id, pin: `${c.id}.${pin.id}` })) ?? [])
        .map((k) => nodes.get(k)).filter((n): n is PinNode => !!n && n.end.item !== placed.id))
      const far = ends.flat()
      if (far.length === 0) continue
      const local = (ns: readonly PinNode[]) => toPart(placed.part, placed, { x: ns.reduce((s, n) => s + n.at.x, 0) / ns.length, y: ns.reduce((s, n) => s + n.at.y, 0) / ns.length })
      const laid = layLead(c, local(far), ends.map((ns) => (ns.length ? local(ns) : undefined)))
      leads.set(`${placed.id}:${c.id}`, laid)
      const exit = turnBy(turnBy({ x: 0, y: 1 }, laid.rotation ?? 0), placed.rotation)
      connectorPinPoints(laid).forEach((p, i) => {
        const key = endKey({ item: placed.id, pin: `${c.id}.${c.pins[i]!.id}` })
        const node = nodes.get(key)
        if (node) nodes.set(key, { ...node, at: toCanvas(placed.part, placed, p), exit })
      })
    }
  }
  return { parts, nodes, group, members, wired, leads }
}

/** The pins joined to a pin (itself included). */
export const groupOf = (net: Netlist, key: string): readonly PinNode[] =>
  (net.members.get(net.group.get(key) ?? key) ?? []).map((k) => net.nodes.get(k)!).filter(Boolean)

/* ---- devices behind the pins ---- */

export const deviceOf = (ctx: HardwareContext, id: number | undefined): DeviceInfo | undefined => (id === undefined ? undefined : ctx.devices.find((d) => d.id === id))

/** The config field a consumer pin feeds. */
export const consumerField = (ctx: HardwareContext, node: PinNode): WireField | undefined =>
  deviceOf(ctx, node.device)?.type?.config?.fields.find((f) => f.id === node.pin.config)

/** The modes a provider's pin can take (its device type's pins). */
export const providerModes = (ctx: HardwareContext, node: PinNode): readonly string[] => devicePins(deviceOf(ctx, node.device))?.modes ?? []

/** Who else uses a provider's pin (a device other than `except`): the reason it is not free. */
export const pinTakenBy = (ctx: HardwareContext, device: number, pin: number, except?: number): string | undefined => {
  if (isReserved(ctx.board, device, pin)) return ctx.board.reservedPins.find((p) => p.device === device && p.pin === pin)?.label ?? 'the board'
  const by = ctx.devices.find((d) => d.id !== except && d.uses.some((u) => u.pin.device === device && u.pin.pin === pin))
  return by && `${by.name} (${by.uses.find((u) => u.pin.device === device && u.pin.pin === pin)?.use ?? 'a pin'})`
}

/** The I2C bus field of a device type. */
export const busField = (type: CatalogEntry | undefined): WireField | undefined => type?.config?.fields.find((f) => f.enum === 'sys_i2c_bus_e')

/** The pins a wire may go on (`takesWires`) nearest to a point (mm) first; `radius` keeps only those within it, `item` only one part's. */
export const pinsNear = (net: Netlist, at: Point, { radius, item }: { radius?: number; item?: string } = {}): PinNode[] =>
  [...net.nodes.values()]
    .filter((n) => takesWires(n.connector) && (item === undefined || n.end.item === item))
    .map((n) => ({ n, d: Math.hypot(n.at.x - at.x, n.at.y - at.y) }))
    .filter((e) => radius === undefined || e.d <= radius)
    .sort((a, b) => a.d - b.d)
    .map((e) => e.n)
