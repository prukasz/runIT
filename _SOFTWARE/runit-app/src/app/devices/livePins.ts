import { requestFrame, Watches } from '@backend'
import type { RunitLink, ServiceCall } from '@backend'

/*
 * The pins followed live: each read is one watch on the board (SYS_WATCH.MD), shared by whoever shows it (the pin's row and
 * the pin's panel), with the last half minute of its values. The first who follows a read adds the watch, the last to stop
 * removes it; the board then reads by itself and sends a value when it changed.
 */

/** How often the board reads a followed pin, and the longest it stays silent when nothing changes. */
export const LIVE_PERIOD_MS = 200
export const LIVE_SILENCE_MS = 2000
/** How much of a pin's past is kept and drawn. */
export const LIVE_WINDOW_MS = 30_000

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

interface Entry {
  history: PinHistory
  readonly listeners: Set<(history: PinHistory) => void>
  /** Resolves with the watch's removal; undefined when it was not added. */
  readonly removal: Promise<(() => Promise<void>) | undefined>
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
  private readonly entries = new Map<string, Entry>()

  constructor(link: RunitLink) {
    this.watches = new Watches(link)
  }

  /** Follows a pin's read (`field`: the value in its answer); `listener` gets the history at once and at each value. Returns the stop. */
  follow(call: ServiceCall, field: string, listener: (history: PinHistory) => void): () => void {
    const key = requestFrame(call.request).join(',')
    const entry = this.entries.get(key) ?? this.add(key, call, field)
    entry.listeners.add(listener)
    listener(entry.history)
    return () => {
      entry.listeners.delete(listener)
      // A moment later: a view drawn anew (the row after an edit, the panel for the same pin) follows it again at once, and keeps the watch.
      setTimeout(() => this.drop(key, entry), 0)
    }
  }

  private drop(key: string, entry: Entry): void {
    if (entry.listeners.size > 0 || this.entries.get(key) !== entry) return
    this.entries.delete(key)
    // A removal that fails goes with the session.
    void entry.removal.then((remove) => remove?.()).catch(() => undefined)
  }

  private add(key: string, call: ServiceCall, field: string): Entry {
    const tell = (history: PinHistory) => {
      entry.history = history
      for (const listener of entry.listeners) listener(history)
    }
    const removal = this.watches
      .follow({ ...call, label: `${call.label} (live)` }, { trigger: 'change', periodMs: LIVE_PERIOD_MS, silenceMs: LIVE_SILENCE_MS }, (answer) => {
        const raw = answer === null || typeof answer !== 'object' ? undefined : (answer as Record<string, unknown>)[field]
        const value = raw === undefined ? undefined : Number(raw)
        const at = Date.now()
        tell({ samples: [...trimSamples(entry.history.samples, at), { at, value }] })
      })
      .catch((failure: unknown) => {
        tell({ samples: [], failed: failure instanceof Error ? failure.message : String(failure) })
        return undefined
      })
    const entry: Entry = { history: NO_HISTORY, listeners: new Set(), removal }
    this.entries.set(key, entry)
    return entry
  }
}
