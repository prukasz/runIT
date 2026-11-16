import { decodePeers } from '../protocol/peers'
import type { SessionPeer } from '../protocol/peers'
import type { CommandOptions } from './commands'
import type { RunitLink } from './link'
import { Services } from './services'

/*
 * The board's sessions (SYS_SESSION.MD), through the SESSION router: its id (its place in the project) and the sessions it has
 * (PEERS: on which provider, to whom, open or not, with which dead-man). The session of this link itself is `RunitLink`'s (its
 * HELLO, `link.session`); this is the board's view of all of them. A session is made and ended by the data matrix's roles
 * (`DATA_MATRIX ROLE`, `CLEAR_ROLE`), which the catalog's commands reach.
 *
 *   const sessions = new Sessions(link)
 *   await sessions.boardId()                                          // 0: the root, by convention
 *   await sessions.setBoardId(1)                                      // kept in NVS, used from the next start
 *   await sessions.peers()                                            // [{ provider, open: true, peerId: 0xf0, deadmanMs: 0, ... }]
 *
 * The commands are the descriptor's (`contract_session.json`), named once in `SESSION_COMMANDS`.
 */

export const SESSION_COMMANDS = { service: 'SESSION', id: 'ID', peers: 'PEERS' } as const

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
}
