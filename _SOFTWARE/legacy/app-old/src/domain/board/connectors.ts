/*
 * The premade connectors of the board creator. Each type knows its size, the position of each pin (millimetres from
 * the connector's centre, before rotation) and how it is drawn. A pin is `<g id><title/><circle/></g>`: the group is
 * the terminal a wire attaches to, its circle the attachment point.
 */

export interface ConnectorPinShape {
  /** The pin's name on this connector type (`1`, `+`, `VBUS`). */
  readonly name: string
  readonly x: number
  readonly y: number
}

export interface ConnectorType {
  readonly id: string
  readonly label: string
  readonly group: 'Screw terminals' | 'Pin headers' | 'Power'
  /** Body size in millimetres (before rotation). */
  readonly width: number
  readonly height: number
  readonly pins: readonly ConnectorPinShape[]
  /** Font size of the pin names under the part, in millimetres (smaller where the pins are close). */
  readonly labelSize: number
  /** The part's markup in millimetres around (0, 0): `terminal(i)` is the pin group, wrapped by the caller's titles. */
  readonly draw: (terminal: (pinIndex: number, shape: string) => string) => string
}

const rect = (x: number, y: number, w: number, h: number, cls: string, r = 0): string => `<rect x="${x}" y="${y}" width="${w}" height="${h}"${r ? ` rx="${r}"` : ''} class="${cls}"/>`
const circle = (cx: number, cy: number, r: number, cls: string): string => `<circle cx="${cx}" cy="${cy}" r="${r}" class="${cls}"/>`
const centred = (index: number, count: number, pitch: number): number => Number(((index - (count - 1) / 2) * pitch).toFixed(3))

/** A screw terminal block of `count` pins at 5.0 mm: the blue block, its screws, the wire entries on the top edge. */
const screw = (count: number): ConnectorType => {
  const pitch = 5
  const width = count * pitch
  const height = 8
  return {
    id: `screw${count}`,
    label: `Screw terminal ${count}-pin`,
    group: 'Screw terminals',
    width,
    height,
    labelSize: 1.1,
    pins: Array.from({ length: count }, (_, i) => ({ name: String(i + 1), x: centred(i, count, pitch), y: 0.6 })),
    draw: (terminal) =>
      rect(-width / 2, -height / 2, width, height, 'c-screw-body', 0.4) +
      Array.from({ length: count }, (_, i) => {
        const x = centred(i, count, pitch)
        return rect(x - 1.6, -height / 2 + 0.3, 3.2, 1.6, 'c-entry', 0.3) + terminal(i, circle(x, 0.6, 1.9, 'c-well') + circle(x, 0.6, 1.5, 'c-screw') + `<line x1="${x - 0.9}" y1="${0.6 - 0.25}" x2="${x + 0.9}" y2="${0.6 + 0.25}" class="c-slot"/>`)
      }).join(''),
  }
}

/** A pin header, 2.54 mm: black plastic with a gold pad per pin, numbered row by row from the top left. */
const header = (rows: number, cols: number): ConnectorType => {
  const pitch = 2.54
  const width = cols * pitch
  const height = rows * pitch
  const pins = Array.from({ length: rows * cols }, (_, i) => ({ name: String(i + 1), x: centred(i % cols, cols, pitch), y: centred(Math.floor(i / cols), rows, pitch) }))
  return {
    id: `header${rows}x${cols}`,
    label: `Pin header ${rows}x${cols}`,
    group: 'Pin headers',
    width,
    height,
    labelSize: 0.85,
    pins,
    draw: (terminal) =>
      rect(-width / 2, -height / 2, width, height, 'c-header-body', 0.2) +
      pins.map((pin, i) => terminal(i, rect(pin.x - 0.8, pin.y - 0.8, 1.6, 1.6, 'c-gold') + circle(pin.x, pin.y, 0.45, 'c-hole'))).join(''),
  }
}

const xt60: ConnectorType = {
  id: 'xt60',
  label: 'XT60 power',
  group: 'Power',
  width: 15.5,
  height: 8,
  labelSize: 1.1,
  pins: [{ name: '+', x: -3.6, y: 0 }, { name: '-', x: 3.6, y: 0 }],
  draw: (terminal) =>
    `<path d="M -7.75 -4 H 7.75 V 2.6 L 5.4 4 H -5.4 L -7.75 2.6 Z" class="c-xt-body"/>` +
    terminal(0, rect(-5.1, -1.6, 3, 3.2, 'c-xt-pin', 0.5) + circle(-3.6, 0, 0.9, 'c-xt-dot')) +
    terminal(1, rect(2.1, -1.6, 3, 3.2, 'c-xt-pin', 0.5) + circle(3.6, 0, 0.9, 'c-xt-dot')),
}

/** USB-C as a power input: the shell, and the two pins that carry power (VBUS and GND). */
const usbc: ConnectorType = {
  id: 'usbc',
  label: 'USB-C power',
  group: 'Power',
  width: 9,
  height: 7.3,
  labelSize: 1,
  pins: [{ name: 'VBUS', x: -2.2, y: 0 }, { name: 'GND', x: 2.2, y: 0 }],
  draw: (terminal) =>
    rect(-4.5, -3.65, 9, 7.3, 'c-usb-body', 1) +
    rect(-3.7, -1.2, 7.4, 2.4, 'c-usb-slot', 1.2) +
    terminal(0, circle(-2.2, 0, 0.8, 'c-usb-pad')) +
    terminal(1, circle(2.2, 0, 0.8, 'c-usb-pad')),
}

export const CONNECTOR_TYPES: readonly ConnectorType[] = [
  screw(1), screw(2), screw(3), screw(4),
  header(1, 2), header(1, 3), header(1, 4), header(1, 6), header(1, 8), header(2, 3), header(2, 4),
  xt60, usbc,
]

const byId = new Map(CONNECTOR_TYPES.map((type) => [type.id, type]))
export const connectorType = (id: string): ConnectorType | undefined => byId.get(id)

/** The rules the connector art is styled with (one rule per line, so the app can scope them). */
export const CONNECTOR_STYLE = [
  '.pcb { fill: #33383f; stroke: #565d66; stroke-width: 0.25; }',
  '.hole-ring { fill: #d8b25a; }',
  '.hole-bore { fill: #14161a; }',
  '.c-screw-body { fill: #3b8ed8; stroke: #2a6aa6; stroke-width: 0.2; }',
  '.c-entry { fill: #14161a; }',
  '.c-well { fill: #2a6aa6; }',
  '.c-screw { fill: #c9ced6; stroke: #8b929c; stroke-width: 0.15; }',
  '.c-slot { stroke: #5a616b; stroke-width: 0.3; stroke-linecap: round; }',
  '.c-header-body { fill: #1b1c20; stroke: #3a3d44; stroke-width: 0.15; }',
  '.c-gold { fill: #e0b84a; }',
  '.c-hole { fill: #1b1c20; }',
  '.c-xt-body { fill: #e8b923; stroke: #a8830f; stroke-width: 0.2; }',
  '.c-xt-pin { fill: #2d2d2d; }',
  '.c-xt-dot { fill: #d8b25a; }',
  '.c-usb-body { fill: #c9ced6; stroke: #8b929c; stroke-width: 0.2; }',
  '.c-usb-slot { fill: #14161a; }',
  '.c-usb-pad { fill: #d8b25a; }',
  '.pin-label { fill: #fff; stroke: #000; stroke-width: 0.35; paint-order: stroke; font-family: system-ui, sans-serif; font-weight: 600; text-anchor: middle; }',
].join('\n')
