import { createContext, useContext } from 'react'
import type { TelemetryUpdate, TelemetryValue, WatchPlan } from '@engine'

/*
 * What the board says of the running program, kept for the screens. The board sends a value when it changes (VM_CORE.MD
 * "Telemetry is the same command"), a block's When done as a flag that is true on the passes it acted. A pass lasts milliseconds,
 * so a pulse that is shown only while it is true cannot be seen: every cell remembers when it was last true and when it last
 * changed, and `active` / `changed` stay true for `holdMs` after that.
 *
 * A cell is named by what it stands for (`variableKey`, `outputKey`, `doneKey` of the engine's `watchPlan`).
 */

/** How the program is started: on its own (Play), with a live view of the flow and the variables (the eye), or with everything the board can say (the bug). */
export type RunMode = 'run' | 'live' | 'debug'

export type LiveValue = TelemetryValue['value']

interface Cell {
  value: LiveValue
  /** When the value last changed (the app's clock, ms). */
  changedAt: number
  /** When a boolean was last true. */
  trueAt: number | undefined
}

/** A value as a short text: `true`, `12.5`, `[1, 2, 3, …]`, `"text"`. */
export const liveText = (value: LiveValue): string => {
  if (typeof value === 'boolean') return value ? 'true' : 'false'
  if (typeof value === 'number') return Number.isInteger(value) ? String(value) : String(Number(value.toPrecision(5)))
  if (value instanceof Uint8Array) {
    const end = value.indexOf(0)
    return `"${new TextDecoder().decode(end < 0 ? value : value.subarray(0, end))}"`
  }
  const shown = (value as readonly (number | boolean)[]).slice(0, 4).map((v) => liveText(v)).join(', ')
  return `[${shown}${value.length > 4 ? ', …' : ''}]`
}

/** A partial value (a long value is sent in parts: `start` is the first element) put into what the cell held. */
const merged = (old: LiveValue | undefined, update: TelemetryValue): LiveValue => {
  if (update.start === 0 || old === undefined) return update.value
  const piece = update.value
  if (old instanceof Uint8Array && piece instanceof Uint8Array) {
    const next = new Uint8Array(Math.max(old.length, update.start + piece.length))
    next.set(old)
    next.set(piece, update.start)
    return next
  }
  const before = Array.isArray(old) ? [...old] : [old as number | boolean]
  const part = Array.isArray(piece) ? piece : [piece as number | boolean]
  before.splice(update.start, part.length, ...part)
  return before as LiveValue
}

const sameValue = (a: LiveValue, b: LiveValue): boolean => {
  if (a === b) return true
  if (typeof a === 'object' && typeof b === 'object' && a.length === b.length) return Array.from(a as ArrayLike<unknown>).every((v, i) => v === (b as ArrayLike<unknown>)[i])
  return false
}

export class LiveValues {
  private cells = new Map<string, Cell>()

  constructor(public holdMs: number) {}

  /** Takes a telemetry update; true when a cell changed (so a screen has something to redraw). */
  apply(update: TelemetryUpdate, plan: WatchPlan, now: number): boolean {
    let changed = false
    for (const incoming of update.values) {
      if (incoming.type === 'ptr') continue
      for (const key of plan.targets.get(incoming.id) ?? []) {
        const cell = this.cells.get(key)
        const value = merged(cell?.value, incoming)
        const touched = cell === undefined || !sameValue(cell.value, value)
        // The board sends a value again only when it changed, but a repeat of a true flag is still a pulse.
        const pulse = value === true
        if (!touched && !pulse) continue
        this.cells.set(key, { value, changedAt: touched ? now : cell!.changedAt, trueAt: pulse ? now : cell?.trueAt })
        changed = true
      }
    }
    return changed
  }

  value(key: string): LiveValue | undefined {
    return this.cells.get(key)?.value
  }

  /** A flag that is true now, or was within the hold time: a block that acted. */
  active(key: string, now: number): boolean {
    const cell = this.cells.get(key)
    return cell !== undefined && (cell.value === true || (cell.trueAt !== undefined && now - cell.trueAt < this.holdMs))
  }

  /**
   * A boolean as it is shown: true while it is true and for the hold time after (a router drives one branch true for a single pass
   * and clears it on the next, which a pass of milliseconds would never show), false otherwise; undefined for a cell that is not a
   * boolean or has no value yet.
   */
  flag(key: string, now: number): boolean | undefined {
    const cell = this.cells.get(key)
    return cell === undefined || typeof cell.value !== 'boolean' ? undefined : this.active(key, now)
  }

  /** A value that changed within the hold time. */
  changed(key: string, now: number): boolean {
    const cell = this.cells.get(key)
    return cell !== undefined && now - cell.changedAt < this.holdMs
  }

  /** Milliseconds until the next pulse or change stops showing; undefined when none is showing. */
  nextExpiry(now: number): number | undefined {
    let soonest: number | undefined
    for (const cell of this.cells.values()) {
      for (const at of [cell.changedAt, cell.value === true ? undefined : cell.trueAt]) {
        if (at === undefined) continue
        const left = at + this.holdMs - now
        if (left > 0 && (soonest === undefined || left < soonest)) soonest = left
      }
    }
    return soonest
  }

  clear(): void {
    this.cells.clear()
  }
}

/** What a screen reads: the mode being watched and the cells, as of the render. */
export interface LiveView {
  /** The mode of the program on the board while it is watched (`live` or `debug`); undefined otherwise. */
  readonly mode: RunMode | undefined
  /** Objects the board would not follow (too many for its subscription limits). */
  readonly dropped: number
  /** The block the board is parked before, while it is stepped by block (its id in the project). */
  readonly next: string | undefined
  readonly active: (key: string) => boolean
  readonly changed: (key: string) => boolean
  /** A boolean held lit for the hold time after it was true (`LiveValues.flag`); undefined for anything else. */
  readonly flag: (key: string) => boolean | undefined
  /** The value as text, undefined while the board has sent none. */
  readonly text: (key: string) => string | undefined
}

export const NO_LIVE: LiveView = { mode: undefined, dropped: 0, next: undefined, active: () => false, changed: () => false, flag: () => undefined, text: () => undefined }

export const LiveContext = createContext<LiveView>(NO_LIVE)

/** The live view of the running program (`NO_LIVE` outside a watched run). */
export const useLive = (): LiveView => useContext(LiveContext)
