import { decodeStreamList } from '../protocol/streams'
import type { StreamDirection, StreamInfo } from '../protocol/streams'
import type { CommandOptions } from './commands'
import type { RunitLink, StreamFrame } from './link'
import { Services } from './services'

/*
 * The board's data streams (SYS_STREAM.MD), through the STREAM router: what it has (LIST), a stream made or removed, bound to a
 * provider or not, and this client subscribed to a stream so its messages come here. A stream is OUT (the board sends it) or IN
 * (what comes in on its bindings), never both; its targets are bindings (a provider), subscribers (a peer) and the board's code.
 *
 *   const streams = new Streams(link)
 *   await streams.create(0x10, { dir: 'out' })                                  // a user stream, 0x10..0x7F
 *   await streams.bind(0x10, 0x11)                                              // to a provider, by its id: a run-time BLE characteristic, a UART port
 *   const stop = await streams.subscribe(0x10, (data, frame) => …)              // its messages to this client, frame.seq counts them
 *   await stop()                                                                // UNSUBSCRIBE when the last handler goes
 *   const list = await streams.list()                                           // [{ id, dir, urgent, system, targets }]
 *
 * The link subscribes the board's errors, telemetry and events itself when its session opens; `subscribe` of one of those only
 * adds a handler. The board drops a peer's subscriptions when its session ends, so the streams subscribed here are subscribed
 * again after every reconnect. The commands are the descriptor's (`contract_stream.json`), named once in `STREAM_COMMANDS`.
 */

export const STREAM_COMMANDS = {
  service: 'STREAM', create: 'CREATE', remove: 'REMOVE', bind: 'BIND', output: 'OUTPUT', remote: 'REMOTE', unbind: 'UNBIND', unbindAll: 'UNBIND_ALL', list: 'LIST', subscribe: 'SUBSCRIBE', unsubscribe: 'UNSUBSCRIBE',
} as const

export type StreamHandler = (data: Uint8Array, frame: StreamFrame) => void

export class Streams {
  private readonly services: Services
  private readonly handlers = new Map<number, Set<StreamHandler>>()
  private connected: boolean
  private readonly offStream: () => void
  private readonly offStatus: () => void

  constructor(
    private readonly link: RunitLink,
    services?: Services,
  ) {
    this.services = services ?? new Services(link)
    this.connected = link.state === 'connected'
    this.offStream = link.onStream((frame) => {
      for (const handler of this.handlers.get(frame.stream) ?? []) handler(frame.data, frame)
    })
    this.offStatus = link.onStatus((status) => {
      const was = this.connected
      this.connected = status.state === 'connected'
      if (this.connected && !was) void this.subscribeAll()
    })
  }

  /** Every stream of the board with its targets. */
  async list(options?: CommandOptions): Promise<StreamInfo[]> {
    const { data } = await this.services.call(STREAM_COMMANDS.service, STREAM_COMMANDS.list, undefined, options)
    return decodeStreamList(this.link.defs, data)
  }

  /** Makes a stream: `id` in the user range (`defs.stream.userFirst..last`), OUT by default; its direction never changes. */
  async create(id: number, { dir = 'out', urgent = false }: { dir?: StreamDirection; urgent?: boolean } = {}, options?: CommandOptions): Promise<void> {
    const { stream } = this.link.defs
    await this.services.call(STREAM_COMMANDS.service, STREAM_COMMANDS.create, { id, dir: stream.dir[dir], flags: urgent ? stream.flagUrgent : 0 }, options)
  }

  /** Removes a stream of the project (the board's own refuse: `ERR_STREAM_SYSTEM`); its bindings close with it. */
  async remove(id: number, options?: CommandOptions): Promise<void> {
    await this.services.call(STREAM_COMMANDS.service, STREAM_COMMANDS.remove, { id }, options)
  }

  /** Binds a stream to a provider: an OUT stream's messages go there as they are; what comes in there goes into an IN stream. */
  async bind(id: number, provider: number, options?: CommandOptions): Promise<void> {
    await this.services.call(STREAM_COMMANDS.service, STREAM_COMMANDS.bind, { id, provider }, options)
  }

  /** A way out of an IN stream: what comes in goes to this provider as it is. A tunnel between two providers is two IN streams, one each way. */
  async output(id: number, provider: number, options?: CommandOptions): Promise<void> {
    await this.services.call(STREAM_COMMANDS.service, STREAM_COMMANDS.output, { id, provider }, options)
  }

  /** A provider of another board, or a client's receiver, as a target of the stream. Stays when that board is gone. */
  async remote(id: number, board: number, provider: number, options?: CommandOptions): Promise<void> {
    await this.services.call(STREAM_COMMANDS.service, STREAM_COMMANDS.remote, { id, board, provider }, options)
  }

  /** Drops the binding, output or remote target of this provider, on any board. */
  async unbind(id: number, provider: number, options?: CommandOptions): Promise<void> {
    await this.services.call(STREAM_COMMANDS.service, STREAM_COMMANDS.unbind, { id, provider }, options)
  }

  /** Drops every binding, output and remote target of the stream. */
  async unbindAll(id: number, options?: CommandOptions): Promise<void> {
    await this.services.call(STREAM_COMMANDS.service, STREAM_COMMANDS.unbindAll, { id }, options)
  }

  /**
   * The messages of stream `id` to `handler`: the first handler of a stream subscribes this client (SUBSCRIBE), the last one
   * gone unsubscribes it. Resolves with the function that removes the handler.
   */
  async subscribe(id: number, handler: StreamHandler, options?: CommandOptions): Promise<() => Promise<void>> {
    let set = this.handlers.get(id)
    const first = !set
    if (!set) this.handlers.set(id, (set = new Set()))
    set.add(handler)
    if (first && !this.linkOwn(id)) {
      try {
        await this.send(STREAM_COMMANDS.subscribe, id, options)
      } catch (error) {
        this.drop(id, handler)
        throw error
      }
    }
    return async () => {
      if (this.drop(id, handler) && !this.linkOwn(id) && this.link.state === 'connected') await this.send(STREAM_COMMANDS.unsubscribe, id, options)
    }
  }

  /** Stops listening; the board ends this client's subscriptions with its session. */
  close(): void {
    this.offStream()
    this.offStatus()
    this.handlers.clear()
  }

  /** The streams the link subscribes by itself when its session opens. */
  private linkOwn(id: number): boolean {
    const { errors, telemetry, events } = this.link.defs.streams
    return id === errors || id === telemetry || id === events
  }

  /** Removes a handler; true when it was the stream's last. */
  private drop(id: number, handler: StreamHandler): boolean {
    const set = this.handlers.get(id)
    if (!set?.delete(handler)) return false
    if (set.size > 0) return false
    this.handlers.delete(id)
    return true
  }

  private async send(command: string, id: number, options?: CommandOptions): Promise<void> {
    await this.services.call(STREAM_COMMANDS.service, command, { id }, options)
  }

  private async subscribeAll(): Promise<void> {
    for (const id of this.handlers.keys()) {
      if (this.linkOwn(id)) continue
      try {
        await this.send(STREAM_COMMANDS.subscribe, id)
      } catch {
        // The link went again: the next connect subscribes them.
      }
    }
  }
}
