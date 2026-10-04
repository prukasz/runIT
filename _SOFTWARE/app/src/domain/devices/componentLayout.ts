import type { DevicePort } from '../descriptors'

/*
 * Anchor layout of one component variant (board-canvas-concept.md §4a): a background picture (SVG or PNG) and where
 * each pin of the device's pins csv sits on it. The picture carries no ids; a pin left out is not brought out by this
 * variant and cannot be wired. Positions are fractions 0..1 of the picture, so a sharper copy of the picture keeps
 * the layout valid. A placed pin may override the csv's electrical limits for this variant.
 */

/** A variant lives in `app/canvas/<variant>/` of the device folder: its `layout.json` and its picture side by side (the pins csv stays in the device root). */
export const CANVAS_DIR = 'app/canvas'
export const LAYOUT_FILE = 'layout.json'

const DIRS = ['in', 'out', 'inout'] as const
const MODES = ['BINARY', 'PWM', 'ADC', 'DAC'] as const
const IMAGE_PATH = /^[A-Za-z0-9_.-]+\.(svg|png|jpe?g|webp)$/i

/** Angles of a pin's name, in degrees (clockwise on screen; -90 reads upwards): quarter turns only. */
export const LABEL_ANGLES = [0, 90, 180, -90] as const

/** The angle after one quarter turn. */
export const nextLabelAngle = (angle: number): number => LABEL_ANGLES[(LABEL_ANGLES.indexOf(angle as (typeof LABEL_ANGLES)[number]) + 1) % LABEL_ANGLES.length]

const isLabelAngle = (value: unknown): value is number => LABEL_ANGLES.includes(value as never)

export interface AnchorOverrides {
  readonly mV_min?: number
  readonly mV_max?: number
  readonly dir?: DevicePort['dir']
  readonly modes?: readonly string[]
  /** Angle of this pin's name on the picture; 0 when absent. */
  readonly angle?: number
}

export interface Anchor extends AnchorOverrides {
  /** 0..1 of the picture's width / height. */
  readonly x: number
  readonly y: number
}

export interface ComponentLayout {
  /** Picture file name inside the variant's folder, e.g. `breakout.png`. */
  readonly image: string
  /** Picture size on the canvas in whole grid cells. */
  readonly size_cells: readonly [number, number]
  /** Variant name shown in the palette; the variant id when absent. */
  readonly label?: string
  /** Keyed by the pins csv name. */
  readonly anchors: Readonly<Record<string, Anchor>>
}

/** A placed pin: the csv port with this variant's overrides applied. */
export interface ResolvedAnchor {
  readonly port: DevicePort
  readonly x: number
  readonly y: number
  readonly dir: DevicePort['dir']
  readonly modes: readonly string[]
  /** The angle of the pin's name. */
  readonly angle: number
  readonly mV_min?: number
  readonly mV_max?: number
}

export interface ResolvedLayout {
  readonly image: string
  readonly sizeCells: readonly [number, number]
  readonly label?: string
  readonly anchors: readonly ResolvedAnchor[]
  /** csv pins this variant does not bring out. */
  readonly unplaced: readonly string[]
  /** Anchors whose pin the csv no longer has (dropped). */
  readonly dropped: readonly string[]
}

export interface LayoutResult {
  /** Absent when the file is unusable. */
  readonly layout?: ResolvedLayout
  readonly problems: readonly string[]
}

/** Path of a variant's folder inside the device folder. */
export const variantDir = (variant: string): string => `${CANVAS_DIR}/${variant}`

/** Path of a variant's layout inside the device folder: `app/canvas/<variant>/layout.json`. */
export const layoutFileName = (variant: string): string => `${variantDir(variant)}/${LAYOUT_FILE}`

/** The variant a layout file belongs to, or undefined when the path is not `app/canvas/<variant>/layout.json`. */
export function variantOfFile(file: string): string | undefined {
  return /^app\/canvas\/([a-z][a-z0-9_]*)\/layout\.json$/.exec(file)?.[1]
}

const isRecord = (value: unknown): value is Record<string, unknown> => typeof value === 'object' && value !== null && !Array.isArray(value)
const isFraction = (value: unknown): value is number => typeof value === 'number' && Number.isFinite(value) && value >= 0 && value <= 1
const isMillivolt = (value: unknown): value is number => typeof value === 'number' && Number.isInteger(value) && value >= 0

/** Checks a parsed layout file against the device's pins; invalid anchors are reported and skipped. */
export function resolveLayout(raw: unknown, ports: readonly DevicePort[]): LayoutResult {
  const problems: string[] = []
  if (!isRecord(raw)) return { problems: ['Layout is not an object.'] }
  if (typeof raw.image !== 'string' || !IMAGE_PATH.test(raw.image)) return { problems: ['Layout needs an `image` file name like <name>.png.'] }
  const size = raw.size_cells
  if (!Array.isArray(size) || size.length !== 2 || !size.every((n) => Number.isInteger(n) && n > 0)) return { problems: ['Layout needs `size_cells` as two whole numbers above 0.'] }
  if (!isRecord(raw.anchors)) return { problems: ['Layout needs an `anchors` object.'] }

  const byName = new Map(ports.map((port) => [port.name, port]))
  const anchors: ResolvedAnchor[] = []
  const dropped: string[] = []
  for (const [name, entry] of Object.entries(raw.anchors)) {
    const port = byName.get(name)
    if (!port) {
      dropped.push(name)
      continue
    }
    if (!isRecord(entry) || !isFraction(entry.x) || !isFraction(entry.y)) {
      problems.push(`Anchor ${name}: x and y must be numbers from 0 to 1.`)
      continue
    }
    const issue = overrideProblem(entry)
    if (issue) {
      problems.push(`Anchor ${name}: ${issue}`)
      continue
    }
    const mV_min = (entry.mV_min as number | undefined) ?? port.mV_min
    const mV_max = (entry.mV_max as number | undefined) ?? port.mV_max
    if (mV_min !== undefined && mV_max !== undefined && mV_min > mV_max) {
      problems.push(`Anchor ${name}: mV_min is above mV_max.`)
      continue
    }
    anchors.push({
      port,
      x: entry.x,
      y: entry.y,
      dir: (entry.dir as DevicePort['dir'] | undefined) ?? port.dir,
      modes: (entry.modes as string[] | undefined) ?? port.modes ?? [],
      angle: (entry.angle as number | undefined) ?? 0,
      ...(mV_min !== undefined ? { mV_min } : {}),
      ...(mV_max !== undefined ? { mV_max } : {}),
    })
  }
  if (dropped.length) problems.push(`Anchors without a pin in the csv were dropped: ${dropped.join(', ')}.`)
  const placed = new Set(anchors.map((anchor) => anchor.port.name))
  return {
    layout: {
      image: raw.image,
      sizeCells: [size[0], size[1]],
      ...(typeof raw.label === 'string' ? { label: raw.label } : {}),
      anchors,
      unplaced: ports.filter((port) => !placed.has(port.name)).map((port) => port.name),
      dropped,
    },
    problems,
  }
}

function overrideProblem(entry: Record<string, unknown>): string | undefined {
  if (entry.mV_min !== undefined && !isMillivolt(entry.mV_min)) return 'mV_min must be a whole number of millivolts.'
  if (entry.mV_max !== undefined && !isMillivolt(entry.mV_max)) return 'mV_max must be a whole number of millivolts.'
  if (entry.dir !== undefined && !DIRS.includes(entry.dir as never)) return `dir must be one of ${DIRS.join(', ')}.`
  if (entry.angle !== undefined && !isLabelAngle(entry.angle)) return `angle must be one of ${LABEL_ANGLES.join(', ')}.`
  if (entry.modes !== undefined && !(Array.isArray(entry.modes) && entry.modes.every((mode) => MODES.includes(mode as never)))) return `modes must be from ${MODES.join(', ')}.`
  return undefined
}

/** The file text for a layout: ports in csv order, a fixed key order and 4-digit positions, so saved files diff cleanly. */
export function serializeLayout(layout: ComponentLayout, ports: readonly DevicePort[]): string {
  const round = (n: number) => Math.round(n * 10000) / 10000
  const anchors: Record<string, Anchor> = {}
  for (const port of ports) {
    const anchor = layout.anchors[port.name]
    if (!anchor) continue
    anchors[port.name] = {
      x: round(anchor.x),
      y: round(anchor.y),
      ...(anchor.mV_min !== undefined ? { mV_min: anchor.mV_min } : {}),
      ...(anchor.mV_max !== undefined ? { mV_max: anchor.mV_max } : {}),
      ...(anchor.dir !== undefined ? { dir: anchor.dir } : {}),
      ...(anchor.modes !== undefined ? { modes: anchor.modes } : {}),
      ...(anchor.angle ? { angle: anchor.angle } : {}),
    }
  }
  const fields = (anchor: Anchor) => Object.entries(anchor).map(([key, value]) => `${JSON.stringify(key)}: ${JSON.stringify(value).replace(/,/g, ', ')}`).join(', ')
  const lines = Object.entries(anchors).map(([name, anchor]) => `    ${JSON.stringify(name)}: { ${fields(anchor)} }`)
  return ['{', `  "image": ${JSON.stringify(layout.image)},`, `  "size_cells": [${layout.size_cells[0]}, ${layout.size_cells[1]}],`, ...(layout.label ? [`  "label": ${JSON.stringify(layout.label)},`] : []), '  "anchors": {', lines.join(',\n'), '  }', '}', ''].join('\n')
}

/** The editable form of a resolved layout: only the overrides that differ from the csv are kept. */
export function layoutOf(resolved: ResolvedLayout): ComponentLayout {
  const anchors: Record<string, Anchor> = {}
  for (const anchor of resolved.anchors) {
    const { port } = anchor
    anchors[port.name] = {
      x: anchor.x,
      y: anchor.y,
      ...(anchor.mV_min !== port.mV_min && anchor.mV_min !== undefined ? { mV_min: anchor.mV_min } : {}),
      ...(anchor.mV_max !== port.mV_max && anchor.mV_max !== undefined ? { mV_max: anchor.mV_max } : {}),
      ...(anchor.dir !== port.dir ? { dir: anchor.dir } : {}),
      ...(anchor.modes.join('|') !== (port.modes ?? []).join('|') ? { modes: anchor.modes } : {}),
      ...(anchor.angle ? { angle: anchor.angle } : {}),
    }
  }
  return { image: resolved.image, size_cells: resolved.sizeCells, ...(resolved.label ? { label: resolved.label } : {}), anchors }
}

/** Picture size in whole grid cells for a chosen width in cells, keeping the aspect (at least one cell each way). */
export const cellsFor = (cellsWide: number, pixelsWide: number, pixelsHigh: number): [number, number] => {
  const wide = Math.max(1, Math.round(cellsWide))
  return [wide, Math.max(1, Math.round((wide * pixelsHigh) / Math.max(1, pixelsWide)))]
}
