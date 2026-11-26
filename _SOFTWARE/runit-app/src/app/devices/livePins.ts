import { deviceCall, Watches } from '@backend'
import type { Decoded, RunitLink, ServiceCatalog } from '@backend'

/*
 * The pins followed live: the pins of a device read the same way (its levels, its voltages) are one watch on the board
 * (SYS_WATCH.MD) of the IO contract's LEVELS or VOLTAGES, which reads all of them at once. Whoever shows a pin (its row,
 * its panel) follows it; the watch reads the pins followed, and is added again with the new set when it changes, the old one
 * removed. Each pin keeps the last half minute of its values. The board reads by itself and sends the answer when it changed.
 */

/** How often the board reads the followed pins, and the longest it stays silent when nothing changes. */
export const LIVE_PERIOD_MS = 200
export const LIVE_SILENCE_MS = 2000
/** How much of a pin's past is kept and drawn. */
export const LIVE_WINDOW_MS = 30_000

/** How a pin is read: its level (LEVELS) or its voltage (VOLTAGES). */
export type LiveKind = 'level' | 'voltage'

/** The IO command that reads many pins of a kind in one read, and the one-pin read it stands for. */
export const BATCH_READS: Readonly<Record<LiveKind, { readonly command: string; readonly pinCommand: string }>> = {
  level: { command: 'LEVELS', pinCommand: 'LEVEL' },
  voltage: { command: 'VOLTAGES', pinCommand: 'VOLTAGE' },
}

export interface PinSample {
  /** When it came (ms since the epoch). */
  readonly at: number
  /** The level (0, 1) or the millivolts; undefined: the board could not read the pin. */
  readonly value: number | undefined
}

export interface PinHistory {
  /** Oldest first; a value holds until the next one. */
  readonly samples: readonly PinSample[]
  /** The watch was not added: why. */
  readonly failed?: string
}

export const NO_HISTORY: PinHistory = { samples: [] }

/** The samples of the last `windowMs` before `now`, and the one before them: it is the value as the window starts. */
export const trimSamples = (samples: readonly PinSample[], now: number, windowMs = LIVE_WINDOW_MS): readonly PinSample[] => {
  const first = samples.findIndex((s) => s.at >= now - windowMs)
  const from = first < 0 ? samples.length - 1 : first - 1
  return from > 0 ? samples.slice(from) : samples
}

const bit = (mask: unknown, pin: number): boolean | undefined =>
  typeof mask === 'number' || typeof mask === 'bigint' ? ((BigInt(mask) >> BigInt(pin)) & 1n) === 1n : undefined

/**
 * Each pin's value in a batch read's answer: LEVELS' `{read, high}` masks, VOLTAGES' `{pin, voltage_mV}` rows. A pin the
 * answer leaves out, and every pin of a read that failed (`answer` undefined), has the value undefined.
 */
export const batchValues = (kind: LiveKind, answer: Decoded | undefined, pins: readonly number[]): ReadonlyMap<number, number | undefined> => {
  const values = new Map<number, number | undefined>(pins.map((pin) => [pin, undefined]))
  if (kind === 'level' && answer !== null && typeof answer === 'object' && !Array.isArray(answer)) {
    const { read, high } = answer as Record<string, unknown>
    for (const pin of pins) if (bit(read, pin)) values.set(pin, bit(high, pin) ? 1 : 0)
  }
  if (kind === 'voltage' && Array.isArray(answer)) {
    for (const row of answer as readonly Record<string, unknown>[]) {
      const pin = Number(row.pin)
      if (values.has(pin)) values.set(pin, Number(row.voltage_mV))
    }
  }
  return values
}

/** The mask of `pins`, bit n = pin n. */
export const pinMask = (pins: Iterable<number>): bigint => {
  let mask = 0n
  for (const pin of pins) mask |= 1n << BigInt(pin)
  return mask
}

interface Pin {
  history: PinHistory
  readonly listeners: Set<(history: PinHistory) => void>
}

/** The pins of one device read one way, and the watch that reads them. */
interface Group {
  /** The catalog of the latest to follow: the watch's command is built from it. */
  catalog: ServiceCatalog
  readonly device: number
  readonly kind: LiveKind
  readonly pins: Map<number, Pin>
  /** The pins the watch on the board reads, as a key: a change adds it again. */
  watching: string
  /** Resolves with the watch's removal; undefined when it was not added. */
  removal?: Promise<(() => Promise<void>) | undefined>
  /** A new set of pins is to be sent at the next turn: the views of a page follow their pins at once, one watch for them all. */
  pending: boolean
}

export class LivePins {
  private static readonly byLink = new WeakMap<RunitLink, LivePins>()
  /** The one of a link. */
  static of(link: RunitLink): LivePins {
    let live = LivePins.byLink.get(link)
    if (!live) LivePins.byLink.set(link, (live = new LivePins(link)))
    return live
  }

  private readonly watches: Watches
  private readonly groups = new Map<string, Group>()

  constructor(link: RunitLink) {
    this.watches = new Watches(link)
  }

  /** Follows pin `pin` of device `device` read as `kind`; `listener` gets its history at once and at each value. Returns the stop. */
  follow(catalog: ServiceCatalog, device: number, kind: LiveKind, pin: number, listener: (history: PinHistory) => void): () => void {
    const key = `${device}/${kind}`
    let group = this.groups.get(key)
    if (!group) this.groups.set(key, (group = { catalog, device, kind, pins: new Map(), watching: '', pending: false }))
    group.catalog = catalog
    let entry = group.pins.get(pin)
    if (!entry) group.pins.set(pin, (entry = { history: NO_HISTORY, listeners: new Set() }))
    entry.listeners.add(listener)
    listener(entry.history)
    this.schedule(group)
    const followed = group
    return () => {
      entry.listeners.delete(listener)
      // A moment later: a view drawn anew (the row after an edit, the panel for the same pin) follows it again at once, and keeps the watch.
      setTimeout(() => {
        if (entry.listeners.size > 0 || followed.pins.get(pin) !== entry) return
        followed.pins.delete(pin)
        this.schedule(followed)
      }, 0)
    }
  }

  private schedule(group: Group): void {
    if (group.pending) return
    group.pending = true
    setTimeout(() => {
      group.pending = false
      if (this.groups.get(`${group.device}/${group.kind}`) === group) this.update(group)
    }, 0)
  }

  /** The watch read the pins followed now: the same watch while they are the same, else a new one and the old one removed. */
  private update(group: Group): void {
    const pins = [...group.pins.keys()].sort((a, b) => a - b)
    const watching = pins.join(',')
    if (watching === group.watching) return
    const old = group.removal
    group.watching = watching
    group.removal = undefined
    if (pins.length === 0) this.groups.delete(`${group.device}/${group.kind}`)
    else group.removal = this.add(group, pins)
    // A removal that fails goes with the session.
    void old?.then((remove) => remove?.()).catch(() => undefined)
  }

  private add(group: Group, pins: readonly number[]): Promise<(() => Promise<void>) | undefined> {
    const tell = (pin: number, history: PinHistory) => {
      const entry = group.pins.get(pin)
      if (!entry) return
      entry.history = history
      for (const listener of entry.listeners) listener(history)
    }
    const read = BATCH_READS[group.kind]
    const call = deviceCall(group.catalog, group.device, 'IO', read.command, { pins: pinMask(pins) }, 'read')
    return this.watches
      .follow({ ...call, label: `${call.label} (live)` }, { trigger: 'change', periodMs: LIVE_PERIOD_MS, silenceMs: LIVE_SILENCE_MS }, (answer) => {
        const at = Date.now()
        for (const [pin, value] of batchValues(group.kind, answer, pins)) {
          const entry = group.pins.get(pin)
          if (entry) tell(pin, { samples: [...trimSamples(entry.history.samples, at), { at, value }] })
        }
      })
      .catch((failure: unknown) => {
        const failed = failure instanceof Error ? failure.message : String(failure)
        for (const pin of pins) tell(pin, { samples: group.pins.get(pin)?.history.samples ?? [], failed })
        return undefined
      })
  }
}
