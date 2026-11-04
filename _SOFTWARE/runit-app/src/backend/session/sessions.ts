import { decodePeers } from '../protocol/streams'
import type { PeerKind, ProviderId, SessionPeer } from '../protocol/streams'
import type { CommandOptions } from './commands'
import type { RunitLink } from './link'
import { Services } from './services'

/*
 * The board's sessions (SYS_SESSION.MD), through the SESSION router: its id (its place in the project, the `dst` of what goes to
 * it), the sessions it has (PEERS: on which provider, to whom, open or not), and a provider made the way to a peer (ATTACH) or
 * not (DETACH). The session of this link itself is `RunitLink`'s (its HELLO, `link.session`); this is the board's view of all
 * of them: a board with others on a cable has its master's or its slaves' sessions there.
 *
 *   const sessions = new Sessions(link)
 *   await sessions.boardId()                                          // 0: the root, by convention
 *   await sessions.setBoardId(1)                                      // kept in NVS, used from the next start
 *   await sessions.peers()                                            // [{ provider, kind: 'client', open: true, peerId: 0xf0, timeoutMs }]
 *   await sessions.attach({ link: 1, channel: 1 }, 'slave')           // a UART port to another board, below this one
 *
 * The commands are the descriptor's (`contract_session.json`), named once in `SESSION_COMMANDS`.
 */

export const SESSION_COMMANDS = { service: 'SESSION', id: 'ID', attach: 'ATTACH', detach: 'DETACH', peers: 'PEERS' } as const

export class Sessions {
  private readonly services: Services

  constructor(
    private readonly link: RunitLink,
    services?: Services,
  ) {
    this.services = services ?? new Services(link)
  }

  /** The board's id: its place in the project (0x00..0xEF). */
  async boardId(options?: CommandOptions): Promise<number> {
    const { value } = await this.services.call(SESSION_COMMANDS.service, SESSION_COMMANDS.id, undefined, { ...options, side: 'read' })
    return Number((value as { board_id: number }).board_id)
  }

  /** Sets the board's id; the board keeps it and takes it at its next start. */
  async setBoardId(id: number, options?: CommandOptions): Promise<void> {
    const last = this.link.defs.session.id.boardLast
    if (!Number.isInteger(id) || id < 0 || id > last) throw new RangeError(`A board's id is 0..${last}, not ${id}.`)
    await this.services.call(SESSION_COMMANDS.service, SESSION_COMMANDS.id, { board_id: id }, { ...options, side: 'write' })
  }

  /** Every session of the board. */
  async peers(options?: CommandOptions): Promise<SessionPeer[]> {
    const { data } = await this.services.call(SESSION_COMMANDS.service, SESSION_COMMANDS.peers, undefined, options)
    return decodePeers(this.link.defs, data)
  }

  /** Makes a provider the way to a peer of this kind; the session opens when both hellos have passed. */
  async attach(provider: ProviderId, kind: PeerKind, options?: CommandOptions): Promise<void> {
    await this.services.call(SESSION_COMMANDS.service, SESSION_COMMANDS.attach, { ...provider, peer: this.link.defs.session.peer[kind] }, options)
  }

  /** Ends the session on a provider and leaves the provider free. */
  async detach(provider: ProviderId, options?: CommandOptions): Promise<void> {
    await this.services.call(SESSION_COMMANDS.service, SESSION_COMMANDS.detach, { ...provider }, options)
  }
}
