import type { CatalogEntry } from '@backend'
import type { DeviceInfo } from '../devices/devices'
import { devicePins } from '../devices/devices'
import type { Connector, ConnectorType, Library, Part, PartPin, Point, StoredPart } from './library'

/*
 * The part creator's model, pure: a draft part (part.json's content) with how its body is drawn, the roles a pin may take (one
 * list for the UI: ground, a power net, a pin of the part's device, a config pin of it, a bus line, a motor lead; on a board a pin
 * of a board device or an H-bridge output), new connectors at the standard sizes, and the files it is saved as.
 */

/** How the body is drawn: a plain shape, a traced photo (its SVG), or the drawing the part already has. */
export type BodySource =
  | { readonly kind: 'shape'; readonly colour: string; readonly radius: number }
  | { readonly kind: 'traced'; readonly svg: string }
  | { readonly kind: 'keep'; readonly svg: string }

export interface Draft {
  readonly part: Part
  readonly body: BodySource
}

export const slug = (name: string): string => name.toLowerCase().normalize('NFKD').replace(/[^a-z0-9]+/g, '_').replace(/^_+|_+$/g, '') || 'part'

export const newDraft = (): Draft => ({
  part: { id: 'my_part', name: 'My part', category: 'module', size_mm: { w: 40, h: 25 }, nets: [], connectors: [] },
  body: { kind: 'shape', colour: '#2f6db5', radius: 1.5 },
})

export const draftOf = (part: StoredPart): Draft => {
  const { svg, ...rest } = part
  return { part: rest, body: { kind: 'keep', svg } }
}

/* ---- connectors ---- */

export const CONNECTOR_LABELS: Readonly<Record<ConnectorType, string>> = {
  goldpin: 'Goldpin header', goldpin_female: 'Female header', screw_terminal: 'Screw terminal', wire: 'Wire lead', xt60: 'XT60', usb_c: 'USB-C', pad: 'Pads',
}

export const newConnector = (part: Part, type: ConnectorType, at: Point): Connector => {
  const n = part.connectors.filter((c) => c.type === type).length + 1
  const id = `${type.replace('_', '')}${n}`
  const count = type === 'xt60' ? 2 : type === 'usb_c' ? 4 : type === 'wire' ? 3 : type === 'screw_terminal' ? 2 : 4
  const base: Connector = { id, label: `${CONNECTOR_LABELS[type]} ${n}`, type, at, rotation: 0, pins: [] }
  const shaped: Connector = type === 'screw_terminal' ? { ...base, pitch_mm: 5.0 } : type === 'wire' ? { ...base, from: { x: at.x, y: Math.max(0, at.y - 10) } } : type === 'goldpin' || type === 'goldpin_female' ? { ...base, rows: 1 } : base
  return withPinCount(shaped, count)
}

/** A connector with `count` pins: new ones are numbered and not connected, the last ones dropped. */
export const withPinCount = (c: Connector, count: number): Connector => {
  const n = Math.max(1, Math.min(80, Math.round(count)))
  const pins: PartPin[] = c.pins.slice(0, n)
  while (pins.length < n) {
    const i = pins.length + 1
    pins.push({ id: `p${i}`, label: `${i}`, nc: true, ...(c.type === 'pad' ? { at: { x: c.at.x + (i - 1) * 1.27, y: c.at.y } } : {}) })
  }
  return { ...c, pins }
}

/* ---- pin roles ---- */

const ROLE_KEYS = ['gnd', 'net', 'device_pin', 'config', 'bus', 'hbridge', 'motor', 'nc'] as const

/** A pin's role as one string: `gnd`, `nc`, `net:<id>`, `self:<pin>`, `dev:<device>:<pin>`, `config:<field>`, `bus:<type>:<line>`, `motor:a`, `hb:<device>:<channel>:<supply>`. */
export const roleOf = (pin: PartPin): string => {
  if (pin.gnd) return 'gnd'
  if (pin.net !== undefined) return `net:${pin.net}`
  if (pin.device_pin) return pin.device_pin.device === 'self' ? `self:${pin.device_pin.pin}` : `dev:${pin.device_pin.device}:${pin.device_pin.pin}`
  if (pin.config !== undefined) return `config:${pin.config}`
  if (pin.bus) return `bus:${pin.bus.type}:${pin.bus.line}${pin.bus.bus !== undefined ? `:${pin.bus.bus}` : ''}`
  if (pin.motor) return `motor:${pin.motor}`
  if (pin.hbridge) return `hb:${pin.hbridge.device}:${pin.hbridge.channel}:${pin.hbridge.supply ?? ''}`
  return 'nc'
}

/** The pin with a role from `roleOf`'s strings (its other role fields gone). */
export const withRole = (pin: PartPin, role: string): PartPin => {
  const rest = Object.fromEntries(Object.entries(pin).filter(([k]) => !(ROLE_KEYS as readonly string[]).includes(k))) as PartPin
  const [kind, a = '', b = '', c] = role.split(':')
  switch (kind) {
    case 'gnd': return { ...rest, gnd: true }
    case 'net': return { ...rest, net: a }
    case 'self': return { ...rest, device_pin: { device: 'self', pin: Number(a) } }
    case 'dev': return { ...rest, device_pin: { device: Number(a), pin: Number(b) } }
    case 'config': return { ...rest, config: a }
    case 'bus': return { ...rest, bus: { type: a as 'i2c', line: b, ...(c !== undefined && c !== '' ? { bus: Number(c) } : {}) } }
    case 'motor': return { ...rest, motor: a === 'b' ? 'b' : 'a' }
    case 'hb': return { ...rest, hbridge: { device: Number(a), channel: Number(b), ...(c ? { supply: c } : {}) } }
    default: return { ...rest, nc: true }
  }
}

export interface RoleChoice {
  readonly value: string
  readonly label: string
  readonly group: string
}

const BUS_LINES: readonly [string, string][] = [['i2c:sda', 'I2C SDA'], ['i2c:scl', 'I2C SCL'], ['spi:mosi', 'SPI MOSI'], ['spi:miso', 'SPI MISO'], ['spi:sck', 'SPI SCK'], ['spi:cs', 'SPI CS'], ['uart:tx', 'UART TX'], ['uart:rx', 'UART RX']]

/**
 * What a pin of the draft may be: ground, not connected, each power net, each pin of the part's device (`self`), each pin field
 * of its config, the bus lines, a motor's leads; on a board, each pin of the board's devices and their H-bridge outputs.
 */
export const roleChoices = (part: Part, type: CatalogEntry | undefined, boardDevices: readonly DeviceInfo[]): RoleChoice[] => {
  const out: RoleChoice[] = [{ value: 'nc', label: 'Not connected', group: 'General' }, { value: 'gnd', label: 'GND', group: 'General' }]
  for (const n of part.nets ?? []) out.push({ value: `net:${n.id}`, label: `${n.label} (${n.role})`, group: 'Power' })
  const pins = devicePins(type ? { type, config: {} } : undefined)
  for (let i = 0; i < (pins?.count ?? 0); i++) out.push({ value: `self:${i}`, label: `${pins?.labels?.[i] ?? `#${i}`} of its device`, group: 'Its device' })
  for (const f of type?.config?.fields ?? []) if (f.pinRef) out.push({ value: `config:${f.id}`, label: `${f.label} (takes a pin${f.requires ? `: ${f.requires}` : ''})`, group: 'Its device' })
  for (const [v, l] of BUS_LINES) out.push({ value: `bus:${v}`, label: l, group: 'Bus' })
  out.push({ value: 'motor:a', label: 'Motor lead A', group: 'Motor' }, { value: 'motor:b', label: 'Motor lead B', group: 'Motor' })
  if (part.category === 'board') {
    for (const d of boardDevices) {
      const dp = devicePins(d)
      for (let i = 0; i < (dp?.count ?? 0); i++) out.push({ value: `dev:${d.id}:${i}`, label: `${d.name} · ${dp?.labels?.[i] ?? `#${i}`}`, group: `Board: ${d.name}` })
      if (d.type?.contracts.includes('HBRIDGE')) for (let ch = 0; ch < 4; ch++) out.push({ value: `hb:${d.id}:${ch}:`, label: `${d.name} · OUT${ch + 1}`, group: `Board: ${d.name}` })
    }
  }
  return out
}

/* ---- the files ---- */

const inner = (svg: string): { box: string; body: string } => {
  const box = /viewBox="([^"]+)"/.exec(svg)?.[1] ?? '0 0 10 10'
  return { box, body: svg.replace(/^[\s\S]*?<svg[^>]*>/, '').replace(/<\/svg>\s*$/, '') }
}

/** The part's drawing: its body (the chips and the connectors are drawn by the app from part.json). */
export const buildSvg = (draft: Draft): string => {
  const { w, h } = draft.part.size_mm
  const body = draft.body.kind === 'shape'
    ? `<rect x="0" y="0" width="${w}" height="${h}" rx="${draft.body.radius}" fill="${draft.body.colour}" stroke="#00000055" stroke-width="0.3"/>`
    : (() => { const { box, body: b } = inner(draft.body.svg); return draft.body.kind === 'keep' ? b : `<svg x="0" y="0" width="${w}" height="${h}" viewBox="${box}" preserveAspectRatio="none">${b}</svg>` })()
  return `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 ${w} ${h}" width="${w}mm" height="${h}mm">
${body.trim()}
</svg>
`
}

/** The part as the library keeps it: part.json and part.svg. */
export const partFiles = (draft: Draft): { id: string; json: string; svg: string; stored: StoredPart } => {
  const svg = buildSvg(draft)
  const part = draft.part
  const clean: Part = { ...part, nets: part.nets?.length ? part.nets : undefined, chips: part.chips?.length ? part.chips : undefined } as Part
  const json = JSON.stringify({ $schema: '../../../templates/part.schema.json', kind: 'part', ...JSON.parse(JSON.stringify(clean)), graphic: 'part.svg' }, null, 2) + '\n'
  return { id: part.id, json, svg, stored: { ...clean, svg } }
}

/** What is wrong with a draft before it can be saved. */
export const draftProblems = (draft: Draft, library: Library): string[] => {
  const p = draft.part
  const out: string[] = []
  if (!/^[a-z0-9_]+$/.test(p.id)) out.push('The id may hold only a-z, 0-9 and _.')
  const other = library.get(p.id)
  if (other && other.origin === 'library' && draft.body.kind !== 'keep') out.push(`The library has a part "${other.name}" with this id: another id, or it is replaced.`)
  if (!p.name.trim()) out.push('The part needs a name.')
  if (p.size_mm.w <= 0 || p.size_mm.h <= 0) out.push('The part needs a size.')
  const ids = new Set<string>()
  for (const c of p.connectors) {
    if (ids.has(c.id)) out.push(`Two connectors are called ${c.id}.`)
    ids.add(c.id)
    for (const pin of c.pins) if (pin.config !== undefined && !p.device) out.push(`${c.label ?? c.id} ${pin.label}: a config pin needs the part's device.`)
  }
  return out
}
