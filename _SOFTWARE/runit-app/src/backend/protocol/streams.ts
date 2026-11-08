import type { LinkDefinitions } from '../definitions'

/*
 * What the STREAM router's LIST and the SESSION router's PEERS answer (their descriptors describe these answers only in words:
 * `u8 count`, then a record per item). The layouts are the firmware's packed structs:
 *
 *   STREAM LIST   [count] then per stream  sys_stream_head_t   [id][dir][flags][state][targets]          5 B
 *                                           then its targets     [type][peer][provider][dropped u16]       5 B each
 *   SESSION PEERS [count] then per session  sys_session_info_t   [provider][peer][open][peer id][timeout u16]  6 B
 *
 * (sys_stream/internal/sys_stream_internal.h, sys_session/router/sys_session_router.h). Little endian.
 */

export type StreamDirection = 'out' | 'in'

/** One target of a stream: a binding (a provider), an IN stream's output (a provider), a subscriber (a peer: a board or a client), or the board's own code. */
export interface StreamTarget {
  readonly kind: 'provider' | 'output' | 'remote' | 'peer' | 'callback'
  /** A subscriber's id; a remote target's board or client. */
  readonly peer?: number
  /** A binding's, an output's or a remote target's provider: its id. */
  readonly provider?: number
  /** Messages not given to it (busy, down, too long, no route); stops at 0xFFFF. */
  readonly dropped: number
}

/** A stream of the board as LIST tells it. */
export interface StreamInfo {
  /** Its id: the first byte of each of its frames. */
  readonly id: number
  /** OUT: the board sends it; IN: what comes in on its bindings. */
  readonly dir: StreamDirection
  readonly urgent: boolean
  /** The board's own (logs, errors, telemetry, events): never removed. */
  readonly system: boolean
  readonly targets: readonly StreamTarget[]
}

const HEAD_BYTES = 5
const TARGET_BYTES = 5

const u16 = (d: Uint8Array, at: number): number => d[at]! | (d[at + 1]! << 8)

/** STREAM LIST's answer; a record cut short ends the list. */
export const decodeStreamList = (defs: LinkDefinitions, data: Uint8Array): StreamInfo[] => {
  const { stream } = defs
  const out: StreamInfo[] = []
  let at = 1
  for (let n = 0; n < (data[0] ?? 0) && at + HEAD_BYTES <= data.length; n++) {
    const [id, dir, flags, state, count] = [data[at]!, data[at + 1]!, data[at + 2]!, data[at + 3]!, data[at + 4]!]
    at += HEAD_BYTES
    const targets: StreamTarget[] = []
    for (let t = 0; t < count && at + TARGET_BYTES <= data.length; t++, at += TARGET_BYTES) {
      const type = data[at]!
      const dropped = u16(data, at + 3)
      if (type === stream.target.peer) targets.push({ kind: 'peer', peer: data[at + 1]!, dropped })
      else if (type === stream.target.provider) targets.push({ kind: 'provider', provider: data[at + 2]!, dropped })
      else if (type === stream.target.remote) targets.push({ kind: 'remote', peer: data[at + 1]!, provider: data[at + 2]!, dropped })
      else if (type === stream.target.output) targets.push({ kind: 'output', provider: data[at + 2]!, dropped })
      else targets.push({ kind: 'callback', dropped })
    }
    out.push({ id, dir: dir === stream.dir.in ? 'in' : 'out', urgent: (flags & stream.flagUrgent) !== 0, system: (state & stream.stateSystem) !== 0, targets })
  }
  return out
}

/** What a session's peer is to the board. */
export type PeerKind = 'master' | 'slave' | 'client'

/** One session of the board as PEERS tells it. */
export interface SessionPeer {
  /** The id of the provider the session runs on. */
  readonly provider: number
  readonly kind: PeerKind
  /** Both hellos passed. */
  readonly open: boolean
  /** The peer's id from its hello; `undefined` before it (`SYS_SESSION_ID_NONE`). */
  readonly peerId?: number
  /** The board ends the session after this much silence from the peer. */
  readonly timeoutMs: number
}

const PEER_BYTES = 6

/** SESSION PEERS's answer; a record cut short ends the list. */
export const decodePeers = (defs: LinkDefinitions, data: Uint8Array): SessionPeer[] => {
  const { peer, id } = defs.session
  const out: SessionPeer[] = []
  for (let n = 0, at = 1; n < (data[0] ?? 0) && at + PEER_BYTES <= data.length; n++, at += PEER_BYTES) {
    const kind: PeerKind = data[at + 1] === peer.master ? 'master' : data[at + 1] === peer.slave ? 'slave' : 'client'
    const peerId = data[at + 3]!
    out.push({
      provider: data[at]!,
      kind,
      open: data[at + 2] === 1,
      ...(peerId === id.none ? {} : { peerId }),
      timeoutMs: u16(data, at + 4),
    })
  }
  return out
}
