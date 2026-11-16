import type { LinkDefinitions } from '../definitions'

/*
 * What SESSION PEERS answers (its descriptor, `contract_session.json`, describes the answer only in words: `u8 count`, then a
 * record per session). The layout is the firmware's packed struct, little endian (sys_session_router.h, SYS_SESSION.MD):
 *
 *   sys_session_info_t   [provider u8][open u8][peer id u8][safety u8][build id u32][dead-man ms u16]     10 B
 */

/** One session of the board as PEERS tells it. */
export interface SessionPeer {
  /** The id of the provider the session runs on. */
  readonly provider: number
  /** The peer's hello has come. */
  readonly open: boolean
  /** The peer's id from its hello; `undefined` before it (`SYS_SESSION_ID_NONE`). */
  readonly peerId?: number
  /** The end of the session is a safety event: the board enters its safe state. */
  readonly safety: boolean
  /** The peer's build (a board's; 0 for a client). */
  readonly buildId: number
  /** The board ends the session after this much silence from the peer; 0: never. */
  readonly deadmanMs: number
}

const PEER_BYTES = 10

/** SESSION PEERS's answer; a record cut short ends the list. */
export const decodePeers = (defs: LinkDefinitions, data: Uint8Array): SessionPeer[] => {
  const out: SessionPeer[] = []
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
  for (let n = 0, at = 1; n < (data[0] ?? 0) && at + PEER_BYTES <= data.length; n++, at += PEER_BYTES) {
    const peerId = data[at + 2]!
    out.push({
      provider: data[at]!,
      open: data[at + 1] === 1,
      ...(peerId === defs.session.id.none ? {} : { peerId }),
      safety: data[at + 3] === 1,
      buildId: view.getUint32(at + 4, true),
      deadmanMs: view.getUint16(at + 8, true),
    })
  }
  return out
}
