import type { FieldValues } from '../catalog/codec'
import { decodeAnswer, requestFrame } from '../protocol/services'
import type { Decoded, ServiceCall } from '../protocol/services'
import { decodeWatchEntry, watchAddCall, watchRemoveCall } from '../protocol/watches'
import type { WatchEntry } from '../protocol/watches'
import type { CommandOptions } from './commands'
import type { RunitLink } from './link'
import { Services } from './services'

/*
 * The watches this client added on the board (SYS_WATCH.MD): one ADD and the board reads and sends by itself, an entry per
 * period, per change or per event, so a view that follows a value asks for nothing more. The board ends a client's watches
 * when its session ends (a reconnect, a restart, the dead-man), so they are added again when the link comes back.
 *
 *   const watches = new Watches(link)
 *   const stop = await watches.add({ head: { trigger: 'SYS_WATCH_ON_CHANGE', period: 10, max_silence: 100 },
 *                                    read: requestFrame(deviceCall(...)) }, (entry, lost) => ...)
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

/** The ticks of a period (`SYS_WATCH_TICK_MS`, 10 ms): at least one. */
const ticks = (ms: number): number => Math.min(0xffff, Math.max(1, Math.round(ms / 10)))

/** Called for each entry; `lost` is the entries of this watch missed since the one before (the gap in the frame's `seq`). */
export type WatchHandler = (entry: WatchEntry, lost: number) => void

interface Held {
  readonly spec: WatchSpec
  readonly handler: WatchHandler
  seq: number | undefined
  /** The board has it: its ADD was answered on this connection. The board ends a client's watches with its session, so a lost link clears it. */
  onBoard: boolean
}

/** The ids in use on one link, shared by every `Watches` of it: the board knows a watch by (owner, id) and an ADD of an id replaces that watch, so two instances must never take the same one. */
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
    this.offStream = link.onStream((frame) => {
      if (frame.name !== 'watches') return
      const entry = decodeWatchEntry(link.defs, frame.data)
      const held = entry && this.held.get(entry.id)
      if (!entry || !held) return
      const seq = frame.seq
      const lost = held.seq === undefined || seq === undefined ? 0 : (seq - held.seq - 1 + 0x10000) & 0xffff
      if (seq !== undefined) held.seq = seq
      held.handler(entry, lost)
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
    const held: Held = { spec, handler, seq: undefined, onBoard: false }
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
      // A watch the board no longer has (the link went since: the session took it) is only forgotten here: a REMOVE of it would
      // be ERR_WATCH_NOT_FOUND on the board.
      if (!held.onBoard || !this.connected) return this.release(id)
      try {
        await this.services.send(watchRemoveCall(this.services.catalog, id), options)
      } finally {
        this.release(id) // free for another only once the board has dropped it
      }
    }
  }

  /**
   * Follows a read (a call of the catalog, a read side): the board runs it by `spec` and each answer comes to `handler` decoded
   * as the command's answer; `undefined` when the read failed on the board. Resolves with the function that stops it.
   */
  follow(call: ServiceCall, spec: FollowSpec, handler: (value: Decoded | undefined) => void, options?: CommandOptions): Promise<() => Promise<void>> {
    const head = {
      trigger: spec.trigger === 'change' ? 'SYS_WATCH_ON_CHANGE' : 'SYS_WATCH_PERIODIC',
      period: ticks(spec.periodMs),
      max_silence: spec.silenceMs === undefined ? 0 : ticks(spec.silenceMs),
      // the event filter is for ON_EVENT: these are its "any" values (SYS_EVENT_ANY_*)
      filter_source: 0xffff,
      filter_event: 0xffff,
      filter_channel: 0xff,
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
    for (let i = 0; i < 255; i++) {
      const id = this.ids.next
      this.ids.next = this.ids.next % 255 + 1
      if (!this.ids.used.has(id)) {
        this.ids.used.add(id)
        return id
      }
    }
    throw new Error('All 255 watches of this client are in use.')
  }

  private send(id: number, spec: WatchSpec, options?: CommandOptions): Promise<unknown> {
    return this.services.send(watchAddCall(this.services.catalog, { ...spec.head, id }, spec.read), options)
  }

  private async addAll(): Promise<void> {
    for (const [id, held] of this.held) {
      held.seq = undefined
      try {
        await this.send(id, held.spec)
        held.onBoard = this.connected
      } catch {
        // The link went again: the next connect adds them.
      }
    }
  }
}
