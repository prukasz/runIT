import type { Connector, ConnectorType, Part, Point, Rotation } from './library'

/*
 * The geometry of the hardware canvas, pure, in millimetres: the standard connectors at their true size (a goldpin is always on a
 * 2.54 mm pitch, whatever the part's drawing says), where each pin of a part is, and where a placed part's pin lands on the canvas.
 * The canvas's unit is the millimetre; the viewport's zoom is pixels per millimetre.
 */

/** The goldpin pitch (0.1 inch): every header, plug and wire lead. */
export const GOLDPIN_PITCH = 2.54
/** The screw terminal pitches a part may use. */
export const SCREW_PITCHES = [3.5, 5.0, 5.08] as const

/** A connector as drawn: a library connector, or a wire lead's laid out towards what it is wired to (any angle, degrees). */
export type DrawnConnector = Omit<Connector, 'rotation'> & {
  readonly rotation?: number
  /** A plug turned over (its pins the other way along its row), so that its wires do not cross. */
  readonly flipped?: boolean
}

/** Per connector type: pitch (mm between pins), and the body round the pins (local units: pins run along +x from 0). */
interface ConnectorShape {
  readonly pitch: number
  /** Body beyond the first and the last pin along x. */
  readonly margin: number
  /** Body from the pins' line towards -y (the wire side of a screw terminal) and towards +y. */
  readonly back: number
  readonly front: number
}

const SHAPES: Readonly<Record<Exclude<ConnectorType, 'pad'>, (c: DrawnConnector) => ConnectorShape>> = {
  goldpin: () => ({ pitch: GOLDPIN_PITCH, margin: GOLDPIN_PITCH / 2, back: GOLDPIN_PITCH / 2, front: GOLDPIN_PITCH / 2 }),
  goldpin_female: () => ({ pitch: GOLDPIN_PITCH, margin: GOLDPIN_PITCH / 2, back: GOLDPIN_PITCH / 2, front: GOLDPIN_PITCH / 2 }),
  wire: () => ({ pitch: GOLDPIN_PITCH, margin: GOLDPIN_PITCH / 2, back: GOLDPIN_PITCH / 2, front: GOLDPIN_PITCH / 2 }),
  screw_terminal: (c) => {
    const pitch = c.pitch_mm ?? 5.0
    // A 5 mm block is about 7.6 mm deep, the screws a little behind the middle; a 3.5 mm one about 7 mm.
    return { pitch, margin: pitch / 2, back: pitch >= 5 ? 4.2 : 3.6, front: pitch >= 5 ? 3.4 : 3.2 }
  },
  xt60: () => ({ pitch: 7.2, margin: 4.3, back: 4.05, front: 4.05 }),
  usb_c: () => ({ pitch: 1.6, margin: (8.94 - 4.8) / 2, back: 3.7, front: 3.7 }),
}

export const pitchOf = (c: DrawnConnector): number => (c.type === 'pad' ? 0 : SHAPES[c.type](c).pitch)

/** Whether wires go on a connector's pins: not a USB-C socket's (it takes a cable; its pins are there for the power, VBUS). */
export const takesWires = (c: DrawnConnector): boolean => c.type !== 'usb_c'

/** How big a pin of a connector is drawn when it is marked (radius, mm): the whole pin, a screw's head, short of the next one. */
export const pinRadius = (c: DrawnConnector): number => (c.type === 'pad' ? 1 : Math.min(2.3, pitchOf(c) * 0.45))

/** Columns and rows of a connector's pins (a goldpin header may have several rows, listed row by row). */
export const gridOf = (c: DrawnConnector): { cols: number; rows: number } => {
  const rows = c.type === 'goldpin' || c.type === 'goldpin_female' ? Math.max(1, c.rows ?? 1) : 1
  return { cols: Math.ceil(c.pins.length / rows), rows }
}

/** A vector turned by a rotation (clockwise on the screen, y down). */
export const turn = (p: Point, rotation: Rotation = 0): Point => {
  switch (rotation) {
    case 90: return { x: -p.y, y: p.x }
    case 180: return { x: -p.x, y: -p.y }
    case 270: return { x: p.y, y: -p.x }
    default: return p
  }
}

/** A vector turned by any angle (degrees, clockwise on the screen); a quarter turn is exact. */
export const turnBy = (p: Point, degrees: number): Point => {
  const quarter = ((degrees % 360) + 360) % 360
  if (quarter % 90 === 0) return turn(p, quarter as Rotation)
  const [cos, sin] = [Math.cos((degrees * Math.PI) / 180), Math.sin((degrees * Math.PI) / 180)]
  return { x: p.x * cos - p.y * sin, y: p.x * sin + p.y * cos }
}

/** Where each pin of a connector is, in the part's millimetres. */
export const connectorPinPoints = (c: DrawnConnector): Point[] => {
  if (c.type === 'pad') return c.pins.map((p) => p.at ?? c.at)
  const pitch = pitchOf(c)
  const { cols } = gridOf(c)
  return c.pins.map((_, i) => {
    const local = { x: (c.flipped ? -1 : 1) * (i % cols) * pitch, y: Math.floor(i / cols) * GOLDPIN_PITCH }
    const t = turnBy(local, c.rotation ?? 0)
    return { x: c.at.x + t.x, y: c.at.y + t.y }
  })
}

/** The body of a connector before its rotation, relative to its first pin: x from `x0` to `x1`, y from `y0` to `y1`. */
export const connectorBody = (c: DrawnConnector): { x0: number; x1: number; y0: number; y1: number } => {
  if (c.type === 'pad') return { x0: -0.6, x1: 0.6, y0: -0.6, y1: 0.6 }
  const shape = SHAPES[c.type](c)
  const { cols, rows } = gridOf(c)
  const [x0, x1] = [-shape.margin, (cols - 1) * shape.pitch + shape.margin]
  return { x0: c.flipped ? -x1 : x0, x1: c.flipped ? -x0 : x1, y0: -shape.back, y1: (rows - 1) * GOLDPIN_PITCH + shape.front }
}

/* ---- placed parts ---- */

/** A part placed on the canvas: its top-left corner (mm) and its rotation about its centre. */
export interface Placement {
  readonly x: number
  readonly y: number
  readonly rotation: Rotation
}

/** The size a placed part takes on the canvas (turned a quarter: width and height swap). */
export const placedSize = (part: Part, rotation: Rotation): { w: number; h: number } =>
  rotation === 90 || rotation === 270 ? { w: part.size_mm.h, h: part.size_mm.w } : { w: part.size_mm.w, h: part.size_mm.h }

/** A placement turned a quarter (90 clockwise, -90 the other way) about the part's centre, which stays where it is (0.5 mm steps). */
export const turnPlacement = (part: Part, place: Placement, by: 90 | -90): Placement => {
  const rotation = ((place.rotation + by + 360) % 360) as Rotation
  const was = placedSize(part, place.rotation)
  const now = placedSize(part, rotation)
  const snap = (v: number) => Math.round(v * 2) / 2
  return { x: snap(place.x + (was.w - now.w) / 2), y: snap(place.y + (was.h - now.h) / 2), rotation }
}

/** A point of the part (its mm) on the canvas. */
export const toCanvas = (part: Part, place: Placement, p: Point): Point => {
  const { w, h } = part.size_mm
  const t = turn({ x: p.x - w / 2, y: p.y - h / 2 }, place.rotation)
  const size = placedSize(part, place.rotation)
  return { x: place.x + size.w / 2 + t.x, y: place.y + size.h / 2 + t.y }
}

/** A point of the canvas in the part's mm (the inverse of toCanvas). */
export const toPart = (part: Part, place: Placement, p: Point): Point => {
  const size = placedSize(part, place.rotation)
  const t = turn({ x: p.x - place.x - size.w / 2, y: p.y - place.y - size.h / 2 }, ((360 - place.rotation) % 360) as Rotation)
  return { x: part.size_mm.w / 2 + t.x, y: part.size_mm.h / 2 + t.y }
}

/**
 * A wire lead laid out towards what it is wired to (`target`, in the part's mm): it leaves the part where it does, its plug
 * turned along the way to the target, as far out as the lead is long (less when the target is nearer), the plug's middle on that
 * line, turned over when that keeps its wires from crossing (`ends`: where each pin's wire goes, in the part's mm). A lead that
 * is not a wire lead, or a target on its exit, stays as the library has it.
 */
export const layLead = (c: Connector, target: Point, ends: readonly (Point | undefined)[] = []): DrawnConnector => {
  if (c.type !== 'wire' || !c.from) return c
  const along = { x: target.x - c.from.x, y: target.y - c.from.y }
  const distance = Math.hypot(along.x, along.y)
  if (distance < 1) return c
  const d = { x: along.x / distance, y: along.y / distance }
  const span = { x: ((c.pins.length - 1) / 2) * GOLDPIN_PITCH, y: 0 }
  const middle = turn(span, c.rotation)
  const length = Math.hypot(c.at.x + middle.x - c.from.x, c.at.y + middle.y - c.from.y)
  const reach = Math.min(length, Math.max(4, distance * 0.7))
  // The plug's +y (where its wire comes from, behind it) points back at the exit: its pins across the way.
  const rotation = (Math.atan2(d.y, d.x) * 180) / Math.PI - 90
  // Pins in order along the row, their wires' ends the other way along it: turned over.
  const across = turnBy({ x: 1, y: 0 }, rotation)
  const order = ends.reduce((sum, end, i) => (end ? sum + (i - (c.pins.length - 1) / 2) * ((end.x - target.x) * across.x + (end.y - target.y) * across.y) : sum), 0)
  const flipped = order < 0
  const half = turnBy({ x: (flipped ? -1 : 1) * span.x, y: 0 }, rotation)
  return { ...c, rotation, flipped, at: { x: c.from.x + d.x * reach - half.x, y: c.from.y + d.y * reach - half.y } }
}

/** Which way a wire lead leaves its part: straight out of the side it leaves from (away from the part's middle, along x or y). */
export const leadOut = (part: Part, c: DrawnConnector): Point | undefined => {
  if (c.type !== 'wire' || !c.from) return undefined
  const v = { x: c.from.x - part.size_mm.w / 2, y: c.from.y - part.size_mm.h / 2 }
  return Math.abs(v.x) >= Math.abs(v.y) ? { x: Math.sign(v.x) || 1, y: 0 } : { x: 0, y: Math.sign(v.y) || 1 }
}

/** The SVG transform that draws the part (its own mm) where it is placed. */
export const partTransform = (part: Part, place: Placement): string => {
  const { w, h } = part.size_mm
  const size = placedSize(part, place.rotation)
  return `translate(${place.x + size.w / 2} ${place.y + size.h / 2}) rotate(${place.rotation}) translate(${-w / 2} ${-h / 2})`
}

/** Every pin of a placed part on the canvas, keyed `connector.pin`. */
export const placedPins = (part: Part, place: Placement): Map<string, Point> => {
  const out = new Map<string, Point>()
  for (const c of part.connectors) {
    connectorPinPoints(c).forEach((p, i) => out.set(`${c.id}.${c.pins[i]!.id}`, toCanvas(part, place, p)))
  }
  return out
}

/** Whether two rectangles overlap. */
export const overlaps = (a: { x: number; y: number; w: number; h: number }, b: { x: number; y: number; w: number; h: number }): boolean =>
  a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h

/* ---- the viewport (screen = canvas mm × zoom + offset) ---- */

export interface Viewport {
  readonly x: number
  readonly y: number
  /** Pixels per millimetre. */
  readonly zoom: number
}

export const ZOOM_MIN = 0.5
export const ZOOM_MAX = 40

export const screenToCanvas = (v: Viewport, p: Point): Point => ({ x: (p.x - v.x) / v.zoom, y: (p.y - v.y) / v.zoom })

export const zoomAt = (v: Viewport, factor: number, anchor: Point): Viewport => {
  const zoom = Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, v.zoom * factor))
  const under = screenToCanvas(v, anchor)
  return { zoom, x: anchor.x - under.x * zoom, y: anchor.y - under.y * zoom }
}

/** The viewport that shows a box (mm) in a screen area (px), with a margin. */
export const fitBox = (box: { x: number; y: number; w: number; h: number }, screen: { w: number; h: number }, margin = 24): Viewport => {
  const zoom = Math.min(ZOOM_MAX, Math.max(ZOOM_MIN, Math.min((screen.w - 2 * margin) / box.w, (screen.h - 2 * margin) / box.h)))
  return { zoom, x: (screen.w - box.w * zoom) / 2 - box.x * zoom, y: (screen.h - box.h * zoom) / 2 - box.y * zoom }
}

/** The smallest box centred on `centre` that holds every box (mm). */
export const boxAround = (centre: Point, boxes: readonly { x: number; y: number; w: number; h: number }[]): { x: number; y: number; w: number; h: number } => {
  const hw = Math.max(1, ...boxes.flatMap((b) => [Math.abs(b.x - centre.x), Math.abs(b.x + b.w - centre.x)]))
  const hh = Math.max(1, ...boxes.flatMap((b) => [Math.abs(b.y - centre.y), Math.abs(b.y + b.h - centre.y)]))
  return { x: centre.x - hw, y: centre.y - hh, w: 2 * hw, h: 2 * hh }
}

/** The view at the same zoom with a canvas point (mm) in the middle of the screen. */
export const centreOn = (view: Viewport, point: Point, screen: { w: number; h: number }): Viewport =>
  ({ ...view, x: screen.w / 2 - point.x * view.zoom, y: screen.h / 2 - point.y * view.zoom })

/** A ruler's length for a zoom: a round number of centimetres that is 80–200 px long. */
export const rulerCm = (zoom: number): number => {
  for (const cm of [0.5, 1, 2, 5, 10, 20, 50]) if (cm * 10 * zoom >= 80) return cm
  return 100
}
