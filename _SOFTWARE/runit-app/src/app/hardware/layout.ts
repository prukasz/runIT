import type { Rotation, StoredPart } from './library'

/*
 * What the Devices canvas keeps in the project (`ProjectFile.hardware`), pure: the parts placed round the board, the wires between
 * their pins, the power choices, and the parts the project carries itself (made in the creator). Like the Code canvas it is the
 * app's own: the board never gets it. What it decides for the board (a servo's PWM pin, a rail's voltage, a mux's source) is
 * written into the project's devices and start commands by `apply.ts`, where the Devices view shows it.
 */

/** The id the board has among the canvas's items. */
export const BOARD_ITEM = 'board'

/** A part on the canvas. */
export interface HardwareItem {
  readonly id: string
  /** Its part (library id). */
  readonly part: string
  readonly x: number
  readonly y: number
  readonly rotation: Rotation
  /** The project device it is (a part with a device): created when it is placed, removed with it. */
  readonly device?: number
}

/** One end of a wire: an item's pin (`connector.pin`). */
export interface PinEnd {
  readonly item: string
  readonly pin: string
}

export interface HardwareWire {
  readonly id: string
  readonly a: PinEnd
  readonly b: PinEnd
}

/** The user's power choices; what is not chosen the canvas picks for what is connected (power.ts). */
export interface PowerChoices {
  /** The source element of the board that feeds it (its id: `src_usb`, `src_ext`); unset: the first. */
  readonly source?: string
  /** The voltage of that source: what the external supply gives, or the USB PD level to ask for (unset: the lowest that fits). */
  readonly input_mV?: number
  /** A mux's input by its element id (0: the first); unset: picked for what it feeds. */
  readonly muxes?: Readonly<Record<string, number>>
  /** An adjustable regulator's voltage the user set by hand, by element id (when nothing on it asks for one). */
  readonly rails?: Readonly<Record<string, number>>
}

export interface HardwareLayout {
  /** Where the board sits (its item is `BOARD_ITEM`). */
  readonly board: { readonly x: number; readonly y: number; readonly rotation: Rotation }
  readonly items: readonly HardwareItem[]
  readonly wires: readonly HardwareWire[]
  readonly power: PowerChoices
  /** Parts made in the creator, carried by the project. */
  readonly parts: readonly StoredPart[]
  /** The board devices whose start commands the canvas wrote (a regulator's VOLTAGE and ENABLE, a mux's SOURCE). */
  readonly managed: readonly number[]
}

export const EMPTY_HARDWARE: HardwareLayout = { board: { x: 0, y: 0, rotation: 0 }, items: [], wires: [], power: {}, parts: [], managed: [] }

const isObject = (v: unknown): v is Record<string, unknown> => typeof v === 'object' && v !== null && !Array.isArray(v)
const isRotation = (v: unknown): v is Rotation => v === 0 || v === 90 || v === 180 || v === 270
const isEnd = (v: unknown): v is PinEnd => isObject(v) && typeof v.item === 'string' && typeof v.pin === 'string'
const num = (v: unknown, fallback = 0): number => (typeof v === 'number' && Number.isFinite(v) ? v : fallback)

/** What makes sense of a stored layout: anything unreadable is left out, never an exception. */
export const readHardware = (data: unknown): HardwareLayout => {
  if (!isObject(data)) return EMPTY_HARDWARE
  const board = isObject(data.board) ? { x: num(data.board.x), y: num(data.board.y), rotation: isRotation(data.board.rotation) ? data.board.rotation : 0 } : EMPTY_HARDWARE.board
  const items = (Array.isArray(data.items) ? data.items : []).filter((i): i is Record<string, unknown> => isObject(i) && typeof i.id === 'string' && typeof i.part === 'string')
    .map((i): HardwareItem => ({ id: i.id as string, part: i.part as string, x: num(i.x), y: num(i.y), rotation: isRotation(i.rotation) ? i.rotation : 0, ...(Number.isInteger(i.device) ? { device: i.device as number } : {}) }))
  const ids = new Set([BOARD_ITEM, ...items.map((i) => i.id)])
  const wires = (Array.isArray(data.wires) ? data.wires : []).filter((w): w is HardwareWire => isObject(w) && typeof w.id === 'string' && isEnd(w.a) && isEnd(w.b) && ids.has(w.a.item) && ids.has(w.b.item))
  const power = isObject(data.power) ? data.power as PowerChoices : {}
  const parts = (Array.isArray(data.parts) ? data.parts : []).filter((p): p is StoredPart => isObject(p) && typeof p.id === 'string' && typeof p.svg === 'string' && Array.isArray(p.connectors) && isObject(p.size_mm))
  const managed = (Array.isArray(data.managed) ? data.managed : []).filter((d): d is number => Number.isInteger(d))
  return { board, items, wires, power, parts, managed }
}

/** A fresh id for an item or a wire: `<prefix><n>`, not taken. */
export const freshId = (taken: Iterable<string>, prefix: string): string => {
  const used = new Set(taken)
  let n = 1
  while (used.has(`${prefix}${n}`)) n++
  return `${prefix}${n}`
}

export const sameEnd = (a: PinEnd, b: PinEnd): boolean => a.item === b.item && a.pin === b.pin
export const endKey = (e: PinEnd): string => `${e.item}:${e.pin}`
