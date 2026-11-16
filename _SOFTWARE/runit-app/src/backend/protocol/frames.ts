import type { LinkDefinitions } from '../definitions'

/*
 * The session's frames (SYS_SESSION.MD "Frames"): one 3-byte header on every frame, then its body. A frame goes to the peer
 * on the link it travels: no addresses.
 *
 *   header    [frame byte][seq u16 LE]                frame byte: bits 0-1 flags, bit 2 an answer's status, bits 6-7 kind
 *   request   [root key][dir][op ...][payload]        seq: this client's counter
 *   answer    [data]                                  status OK: the router's output; ERROR: the error record; seq: the request's
 *   push      [tag][payload]                          a tag below 0x80 is a stream's id; from 0x80 a channel of a module (a watch's
 *                                                     entries, a remote device's frames); seq: the session's count of its pushes
 *   control   [op][data]                              HELLO (Hello), PING, NO_SESSION
 *
 * The same frame travels on every link; the BLE link only fragments it (transport/framing.ts).
 */

export const HEADER_BYTES = 3

export interface Request {
  /** The keys after the header: the root key of a router, then the keys inside it (the side, an op). `defs.keys` has the root keys by name. */
  readonly keys: readonly number[]
  readonly payload?: Uint8Array
  /** Run on the urgent task, next to (not behind) normal frames. */
  readonly urgent?: boolean
  /** No answer: a failure goes to the errors stream only. */
  readonly noReply?: boolean
}

export interface FrameHeader {
  readonly kind: number
  /** A request's or a push's flags (`router_flag`). */
  readonly flags: number
  /** An answer's status bit set: ERROR. */
  readonly error: boolean
  readonly seq: number
}

export interface Frame extends FrameHeader {
  readonly body: Uint8Array
}

const kindShift = (defs: LinkDefinitions): number => Math.log2(defs.session.frame.kind & -defs.session.frame.kind)

/** One frame: the header, then `body`. */
export const encodeFrame = (defs: LinkDefinitions, header: { kind: number; flags?: number; seq: number }, body: Uint8Array): Uint8Array => {
  if (!Number.isInteger(header.seq) || header.seq < 0 || header.seq > 0xffff) throw new RangeError(`Sequence ${header.seq} is outside 0..65535.`)
  const frame = new Uint8Array(HEADER_BYTES + body.length)
  frame[0] = ((header.kind << kindShift(defs)) & defs.session.frame.kind) | ((header.flags ?? 0) & defs.session.frame.flags)
  frame[1] = header.seq & 0xff
  frame[2] = header.seq >> 8
  frame.set(body, HEADER_BYTES)
  return frame
}

/** A frame's header and body; undefined for bytes too short to be one. */
export const decodeFrame = (defs: LinkDefinitions, frame: Uint8Array): Frame | undefined => {
  if (frame.length < HEADER_BYTES) return undefined
  const byte = frame[0]!
  return {
    kind: (byte & defs.session.frame.kind) >> kindShift(defs),
    flags: byte & defs.session.frame.flags,
    error: (byte & defs.session.frame.status) !== 0,
    seq: frame[1]! | (frame[2]! << 8),
    body: frame.subarray(HEADER_BYTES),
  }
}

/** The request frame for `seq`. */
export const encodeRequest = (defs: LinkDefinitions, seq: number, request: Request): Uint8Array => {
  if (request.keys.length === 0) throw new RangeError('A request needs at least its root key.')
  const payload = request.payload ?? new Uint8Array(0)
  const body = new Uint8Array(request.keys.length + payload.length)
  body.set(request.keys, 0)
  body.set(payload, request.keys.length)
  const flags = (request.urgent ? defs.flags.urgent : 0) | (request.noReply ? defs.flags.noReply : 0)
  return encodeFrame(defs, { kind: defs.session.kind.request, flags, seq }, body)
}

export interface Response {
  readonly seq: number
  readonly ok: boolean
  /** The router's output, or the error record when `ok` is false. */
  readonly data: Uint8Array
}

/** The answer in a frame; undefined for any other kind of frame. */
export const decodeResponse = (defs: LinkDefinitions, frame: Uint8Array): Response | undefined => {
  const f = decodeFrame(defs, frame)
  if (!f || f.kind !== defs.session.kind.answer) return undefined
  return { seq: f.seq, ok: !f.error, data: f.body }
}

/* ---- HELLO (sys_session_hello_t, 9 bytes after the control op: sys_session.h) ---- */

export const HELLO_BYTES = 9

/**
 * Who a peer is. Both sides send one when a session opens (SYS_SESSION.MD "A session's life"), each with its own id: the board
 * its place in the project, the app a client id.
 */
export interface Hello {
  /** The sender's own id: a board's place (0x00..0xEF), a client's pick (0xF0..0xFC). */
  readonly id: number
  /** Random per boot: a new one means the sender restarted (and what it had of this session is gone). */
  readonly bootId: number
  /** The first 4 bytes of the firmware's ELF SHA-256: whether two boards run the same build; 0 from a client. */
  readonly buildId: number
}

/** A client's hello: its own id and a boot id of its own. */
export const encodeHello = (defs: LinkDefinitions, hello: { readonly id: number; readonly bootId: number }): Uint8Array => {
  const body = new Uint8Array(1 + HELLO_BYTES)
  const view = new DataView(body.buffer)
  body[0] = defs.session.control.hello
  body[1] = hello.id
  view.setUint32(2, hello.bootId >>> 0, true)
  view.setUint32(6, 0, true)
  return body
}

/** A control frame's HELLO; undefined when the body is not one. */
export const decodeHello = (defs: LinkDefinitions, body: Uint8Array): Hello | undefined => {
  if (body.length < 1 + HELLO_BYTES || body[0] !== defs.session.control.hello) return undefined
  const view = new DataView(body.buffer, body.byteOffset, body.byteLength)
  return { id: body[1]!, bootId: view.getUint32(2, true), buildId: view.getUint32(6, true) }
}
