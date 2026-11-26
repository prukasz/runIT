import { Services } from '@backend'
import type { BleDeviceRef, LinkState, RunitLink } from '@backend'
import { boardLabel } from './board'
import type { RememberedBoard } from './board'

/*
 * The connection to the user's board, with no UI: which board is the user's, finding it, opening the link to it and keeping it.
 *
 *   connect    the remembered board is looked for until it is found (a scan, a pause, a scan again: a board switched on later
 *              is found then), then the link opens to it and keeps itself open (RunitLink's reconnect; the platform finds the board
 *              again by the same finder when its held device no longer connects). With no board remembered the user picks one.
 *   auto       the remembered board is connected when the app starts and as soon as Connect automatically is turned on.
 *   disconnect ends the search and the link (`SYS BYE` first: the board takes it as a goodbye, not a lost link).
 *   forget     disconnects, drops the browser's permission and the remembered board.
 *
 * What finds a board is the platform's (`BoardFinder`: the desktop app scans for its address, a browser opens the board it
 * allowed before); what the app remembers is `RememberedBoard` (board.ts).
 */

/** How a board is found: by the desktop app's scan, or among the boards a browser allowed. */
export interface BoardFinder {
  /** The remembered board, when it is there to open now; undefined when not found this time. Throws `NeedsUser` when only the user can find it. */
  find(board: RememberedBoard): Promise<BleDeviceRef | undefined>
  /** The user picks a board (the desktop app's list, the browser's chooser); rejects when they cancel. */
  choose(): Promise<{ readonly ref: BleDeviceRef; readonly board: RememberedBoard }>
  /** Ends a scan or a choice under way. */
  cancel(): void
  /** Drops the browser's permission for the board (`ref`: the last one opened). */
  forget(ref: BleDeviceRef | undefined): Promise<void>
}

/** The remembered board cannot be found without the user: the chooser must be shown (a browser that no longer allows it). */
export class NeedsUser extends Error {}

export interface BoardStore {
  read(): RememberedBoard | undefined
  write(board: RememberedBoard | undefined): void
}

export interface ConnectionState {
  readonly link: LinkState
  /** Why the board is not connected, or what the search is at. */
  readonly reason?: string
  readonly board?: RememberedBoard
  /** Looking for the remembered board. */
  readonly searching: boolean
  /** The user is picking a board. */
  readonly choosing: boolean
}

/** A scan that did not see the board is tried again after this pause. */
export const SEARCH_PAUSE_MS = 2000

const message = (error: unknown): string => (error instanceof Error ? error.message : String(error))
const cancelled = (error: unknown): boolean => /cancel/i.test(message(error))

export class BoardConnection {
  private state: ConnectionState
  private readonly handlers = new Set<(state: ConnectionState) => void>()
  /** The user wants the board connected: the search goes on until it is found or this is cleared. */
  private wanted = false
  private seeking = false
  private wake: (() => void) | undefined
  private last: BleDeviceRef | undefined
  private readonly offStatus: () => void

  constructor(
    readonly link: RunitLink,
    private readonly finder: BoardFinder,
    private readonly store: BoardStore,
    private readonly pauseMs = SEARCH_PAUSE_MS,
  ) {
    const board = store.read()
    this.state = { link: link.state, searching: false, choosing: false, ...(board ? { board } : {}) }
    this.offStatus = link.onStatus((status) => {
      // The link's own reason while it is up or coming back; the search's while there is no link.
      const reason = status.state === 'connected' ? undefined : status.reason ?? (status.state === 'idle' ? this.state.reason : undefined)
      this.set({ link: status.state, ...(reason ? { reason } : { reason: undefined }) })
    })
  }

  get current(): ConnectionState {
    return this.state
  }

  subscribe(handler: (state: ConnectionState) => void): () => void {
    this.handlers.add(handler)
    return () => this.handlers.delete(handler)
  }

  /** The remembered board, found and opened; with none remembered, the board the user picks. */
  async connect(): Promise<void> {
    if (this.seeking || this.state.choosing || this.link.state !== 'idle') return
    const board = this.state.board
    if (board) return this.seek(board, true)
    return this.choose()
  }

  /** Connect automatically: the remembered board, when there is one and nothing is under way. */
  auto(): void {
    if (this.state.board && !this.seeking && this.link.state === 'idle') void this.seek(this.state.board, false)
  }

  /** The board found again for a link that comes back (the platform's `find`): the remembered one. */
  findAgain(): Promise<BleDeviceRef | undefined> {
    const board = this.state.board
    return board ? this.finder.find(board).catch(() => undefined) : Promise.resolve(undefined)
  }

  async disconnect(): Promise<void> {
    this.stop()
    if (this.link.state === 'connected') {
      await new Services(this.link).call('SYS', 'BYE', undefined, { timeoutMs: 800, label: 'SYS BYE' }).catch(() => undefined)
    }
    await this.link.disconnect()
    this.set({ reason: undefined })
  }

  async forget(): Promise<void> {
    await this.disconnect()
    await this.finder.forget(this.last).catch(() => undefined)
    this.last = undefined
    this.store.write(undefined)
    this.set({ board: undefined, reason: undefined })
  }

  /** Ends the user's choice in the desktop app's list. */
  cancelChoice(): void {
    if (this.state.choosing) this.finder.cancel()
  }

  close(): void {
    this.stop()
    this.offStatus()
  }

  private stop(): void {
    this.wanted = false
    this.finder.cancel()
    this.wake?.()
  }

  /** Looks for `board` until it is found and the link opens, or the user stops it. `byUser`: a board the browser no longer allows is chosen again. */
  private async seek(board: RememberedBoard, byUser: boolean): Promise<void> {
    this.seeking = true
    this.wanted = true
    this.set({ searching: true, reason: undefined })
    try {
      while (this.wanted) {
        let ref: BleDeviceRef | undefined
        try {
          ref = await this.finder.find(board)
        } catch (error) {
          if (!(error instanceof NeedsUser)) throw error
          this.wanted = false
          this.set({ searching: false, reason: error.message })
          if (byUser) {
            this.seeking = false
            await this.choose()
          }
          return
        }
        if (!this.wanted) return
        if (!ref) {
          this.set({ reason: `${boardLabel(board)} is not nearby. Still looking…` })
          await this.pause()
          continue
        }
        if (ref.name && ref.name !== board.name) this.remember({ ...board, name: ref.name })
        this.set({ searching: false, reason: undefined })
        await this.open(ref)
        return
      }
    } catch (error) {
      if (this.wanted) this.set({ reason: message(error) })
    } finally {
      this.seeking = false
      this.set({ searching: false })
    }
  }

  private async choose(): Promise<void> {
    this.set({ choosing: true, reason: undefined })
    try {
      const { ref, board } = await this.finder.choose()
      this.remember(board)
      this.set({ choosing: false })
      this.wanted = true
      await this.open(ref)
    } catch (error) {
      if (!cancelled(error)) this.set({ reason: message(error) })
    } finally {
      this.set({ choosing: false })
    }
  }

  /** Opens the link and keeps it (it reconnects by itself); resolves when it is up, or rejects when the user stops it. */
  private async open(ref: BleDeviceRef): Promise<void> {
    this.last = ref
    await this.link.connect(ref, { retry: true })
  }

  private remember(board: RememberedBoard): void {
    this.store.write(board)
    this.set({ board })
  }

  private pause(): Promise<void> {
    return new Promise((resolve) => {
      const timer = setTimeout(() => { this.wake = undefined; resolve() }, this.pauseMs)
      this.wake = () => { clearTimeout(timer); this.wake = undefined; resolve() }
    })
  }

  private set(change: Partial<ConnectionState>): void {
    const next = { ...this.state, ...change }
    for (const key of Object.keys(next) as (keyof ConnectionState)[]) if (next[key] === undefined) delete next[key]
    if (Object.keys(next).length === Object.keys(this.state).length && (Object.keys(next) as (keyof ConnectionState)[]).every((k) => next[k] === this.state[k])) return
    this.state = next
    for (const handler of this.handlers) handler(next)
  }
}
