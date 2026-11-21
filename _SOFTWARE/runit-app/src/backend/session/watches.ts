import type { FieldValues } from '../catalog/codec'
import { decodeAnswer } from '../protocol/calls'
import type { Decoded, ServiceCall } from '../protocol/calls'
import { requestFrame } from '../protocol/frame'
import { decodeWatchEntry, watchAddCall, watchRemoveCall } from '../protocol/watches'
import type { WatchEntry } from '../protocol/watches'
import type { CommandOptions } from './commands'
import type { RunitLink } from './link'
import { Services } from './services'

/*
 * The watches this client added on the board (SYS_WATCH.MD): one ADD and the board reads and sends by itself, an entry per
 * period or per change, so a view that follows a value asks for nothing more. The entries come on the watches' stream (the raw
 * telemetry characteristic, which carries every owner's): the ones whose owner is this client's id are its own. The board ends
 * a client's watches when its session ends (a reconnect, a restart, the dead-man), so they are added again when the link comes
 * back.
 *
 *   const watches = new Watches(link)
 *   const stop = await watches.follow(deviceCall(...), { trigger: 'change', periodMs: 100, silenceMs: 1000 }, (value) => ...)
 *   await stop()
 */

export interface WatchSpec {
  /** `WATCH ADD`'s head by field id, without `id`: the watch gets one here. */
  readonly head: FieldValues
  /** The read: `[key][dir READ][...]` (`requestFrame` of a read call). */
  readonly read: Uint8Array
}

/** How `follow` reads: every period, or when the answer changed (and anyway after `silenceMs`, so silence never looks dead). */
export interface FollowSpec {
  readonly trigger: 'every' | 'change'
  readonly periodMs: number
  readonly silenceMs?: number
}

/** WATCH ADD's head fields and triggers (`contract_watch.json`, `sys_watch_trigger_e`). */
export const WATCH_HEAD = { trigger: 'trigger', period: 'period', silence: 'max_silence', every: 'SYS_WATCH_PERIODIC', change: 'SYS_WATCH_ON_CHANGE' } as const

/** The board's ticks (`SYS_WATCH_TICK_MS`) of `ms`: at least one, at most a u16. */
const ticks = (ms: number, tickMs: number): number => Math.min(0xffff, Math.max(1, Math.round(ms / tickMs)))

/** Called for each entry. */
export type WatchHandler = (entry: WatchEntry) => void

interface Held {
  readonly spec: WatchSpec
  readonly handler: WatchHandler
  /** The board has it: its ADD was answered on this connection. A lost link takes it with the session. */
  onBoard: boolean
}

/** The ids in use on one link, shared by every `Watches` of it: the board knows a watch by (owner, id), and an ADD of an id replaces that watch. */
interface IdPool {
  next: number
  readonly used: Set<number>
}
const pools = new WeakMap<RunitLink, IdPool>()
const poolOf = (link: RunitLink): IdPool => {
  let pool = pools.get(link)
  if (!pool) pools.set(link, (pool = { next: 1, used: new Set() }))
  return pool
}
/** A watch id is 1..255 (SYS_WATCH.MD). */
const ID_LAST = 255

export class Watches {
  private readonly services: Services
  private readonly held = new Map<number, Held>()
  private readonly ids: IdPool
  private connected: boolean
  private readonly offStream: () => void
  private readonly offStatus: () => void

  constructor(private readonly link: RunitLink) {
    this.ids = poolOf(link)
    this.services = new Services(link)
    this.connected = link.state === 'connected'
    this.offStream = link.on('watches', (data) => {
      const entry = decodeWatchEntry(link.defs, data)
      if (!entry || entry.owner !== link.clientId) return
      this.held.get(entry.id)?.handler(entry)
    })
    this.offStatus = link.onStatus((status) => {
      const was = this.connected
      this.connected = status.state === 'connected'
      if (!this.connected) for (const held of this.held.values()) held.onBoard = false
      if (this.connected && !was) void this.addAll()
    })
  }

  /** Adds a watch; resolves with the function that removes it. */
  async add(spec: WatchSpec, handler: WatchHandler, options?: CommandOptions): Promise<() => Promise<void>> {
    const id = this.freeId()
    const held: Held = { spec, handler, onBoard: false }
    this.held.set(id, held)
    try {
      await this.send(id, spec, options)
      held.onBoard = this.connected
    } catch (error) {
      this.release(id)
      throw error
    }
    return async () => {
      if (!this.held.delete(id)) return
      // A watch the board no longer has (its session went since) is only forgotten: a REMOVE would be ERR_WATCH_NOT_FOUND.
      if (!held.onBoard || !this.connected) return this.release(id)
      try {
        await this.services.send(watchRemoveCall(this.services.catalog, id), options)
      } finally {
        this.release(id) // free for another only once the board has dropped it
      }
    }
  }

  /**
   * Follows a read (a call of the catalog, a read side): the board runs it by `spec` and each answer comes to `handler` decoded as
   * the command answers it; `undefined` when the read failed on the board. Resolves with the function that stops it.
   */
  follow(call: ServiceCall, spec: FollowSpec, handler: (value: Decoded | undefined) => void, options?: CommandOptions): Promise<() => Promise<void>> {
    const head = {
      [WATCH_HEAD.trigger]: spec.trigger === 'change' ? WATCH_HEAD.change : WATCH_HEAD.every,
      [WATCH_HEAD.period]: ticks(spec.periodMs, this.link.defs.watchTickMs),
      [WATCH_HEAD.silence]: spec.silenceMs === undefined ? 0 : ticks(spec.silenceMs, this.link.defs.watchTickMs),
    }
    return this.add({ head, read: requestFrame(call.request) }, (entry) => handler(entry.ok ? decodeAnswer(call, entry.data) : undefined), options)
  }

  /** Stops listening; the board ends the watches with the session. */
  close(): void {
    this.offStream()
    this.offStatus()
    for (const id of this.held.keys()) this.ids.used.delete(id)
    this.held.clear()
  }

  private release(id: number): void {
    this.held.delete(id)
    this.ids.used.delete(id)
  }

  private freeId(): number {
    for (let i = 0; i < ID_LAST; i++) {
      const id = this.ids.next
      this.ids.next = (this.ids.next % ID_LAST) + 1
      if (!this.ids.used.has(id)) {
        this.ids.used.add(id)
        return id
      }
    }
    throw new Error(`All ${ID_LAST} watches of this client are in use.`)
  }

  private send(id: number, spec: WatchSpec, options?: CommandOptions): Promise<unknown> {
    return this.services.send(watchAddCall(this.services.catalog, { ...spec.head, id }, spec.read), options)
  }

  private async addAll(): Promise<void> {
    for (const [id, held] of this.held) {
      try {
        await this.send(id, held.spec)
        held.onBoard = this.connected
      } catch {
        // the link went again: the next connect adds them
      }
    }
  }
}
