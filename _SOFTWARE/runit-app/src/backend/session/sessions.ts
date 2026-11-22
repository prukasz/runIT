import type { FieldRecord } from '../catalog/codec'
import type { ServiceCatalog } from '../catalog/services'
import { bundledServiceCatalog } from '../definitions'
import { decodeAnswer, serviceCall } from '../protocol/calls'
import type { LeadAnswer, ServiceCall } from '../protocol/calls'
import type { CommandOptions } from './commands'
import type { RunitLink } from './link'
import { Services } from './services'

/*
 * The board's sessions (SYS_SESSION.MD "Remote"), through the SESSION router: its id (its place in the project) and the sessions
 * it has (PEERS: on which provider, with whom, open or not, with which dead-man). The session of this link itself is
 * `RunitLink`'s (`link.session`); this is the board's view of all of them. The commands and their fields are the descriptor's
 * (`contract_session.json`), named once in `SESSION_COMMANDS`.
 *
 *   const sessions = new Sessions(link)
 *   await sessions.boardId()                // 0: none given yet
 *   await sessions.setBoardId(1)            // kept in NVS, used from the next start
 *   await sessions.peers()                  // [{ provider, open: true, peerId: 0xf0, deadmanMs: 0, ... }]
 */

export const SESSION_COMMANDS = {
  service: 'SESSION',
  id: 'ID',
  boardId: 'board_id',
  peers: 'PEERS',
  row: { provider: 'provider', open: 'open', peerId: 'peer_id', safety: 'safety', buildId: 'build_id', deadmanMs: 'deadman_ms', role: 'role' },
} as const

/** One session of the board as PEERS tells it. */
export interface SessionPeer {
  /** The provider the session runs on. */
  readonly provider: number
  /** The peer's hello has come. */
  readonly open: boolean
  /** The peer's id from its hello; undefined before it (`CMD_SRC_NONE`). */
  readonly peerId?: number
  /** Its end is a safety event: the board enters its safe state. */
  readonly safety: boolean
  /** The peer's build (a board's; 0 for a client). */
  readonly buildId: number
  /** The board ends the session after this much silence from the peer; 0: never. */
  readonly deadmanMs: number
  /** The peer is a board: the published `session_flag` slave (below this board) or master (above it); 0: a client. */
  readonly role: number
}

export const peersCall = (catalog: ServiceCatalog): ServiceCall => serviceCall(catalog, SESSION_COMMANDS.service, SESSION_COMMANDS.peers)

/** PEERS's answer as its descriptor lays it out (a count, then a row per session). `none` is the peer id before a hello. */
export const readPeers = (call: ServiceCall, data: Uint8Array, none = 0): SessionPeer[] => {
  const { row } = SESSION_COMMANDS
  return ((decodeAnswer(call, data) as LeadAnswer).rows as readonly FieldRecord[]).map((r) => {
    const peerId = Number(r[row.peerId])
    return {
      provider: Number(r[row.provider]),
      open: r[row.open] === true,
      ...(peerId === none ? {} : { peerId }),
      safety: r[row.safety] === true,
      buildId: Number(r[row.buildId]),
      deadmanMs: Number(r[row.deadmanMs]),
      role: Number(r[row.role]),
    }
  })
}

/** PEERS's answer read with the bundled catalog. */
export const decodePeers = (data: Uint8Array, catalog: ServiceCatalog = bundledServiceCatalog()): SessionPeer[] => readPeers(peersCall(catalog), data)

export class Sessions {
  private readonly services: Services

  constructor(
    private readonly link: RunitLink,
    services?: Services,
  ) {
    this.services = services ?? new Services(link)
  }

  /** The board's id: its place in the project (`CMD_SRC_NONE`: none given). */
  async boardId(options?: CommandOptions): Promise<number> {
    const { value } = await this.services.call(SESSION_COMMANDS.service, SESSION_COMMANDS.id, undefined, { ...options, side: 'read' })
    return Number((value as FieldRecord)[SESSION_COMMANDS.boardId])
  }

  /** Sets the board's id; the board keeps it and takes it at its next start. The descriptor's range is checked by the codec. */
  async setBoardId(id: number, options?: CommandOptions): Promise<void> {
    await this.services.call(SESSION_COMMANDS.service, SESSION_COMMANDS.id, { [SESSION_COMMANDS.boardId]: id }, { ...options, side: 'write' })
  }

  /** Every session of the board. */
  async peers(options?: CommandOptions): Promise<SessionPeer[]> {
    const call = peersCall(this.services.catalog)
    return readPeers(call, (await this.services.send(call, options)).data, this.link.defs.src.none)
  }
}
