import type { LinkDefinitions } from '../definitions'

/*
 * The session's frames (SYS_SESSION.MD "Frames"): one 5-byte header on every frame, then its body.
 *
 *   header    [frame byte][dst][src][seq u16 LE]      frame byte: bits 0-1 flags, bit 2 an answer's status, bits 6-7 kind
 *   request   [root key][key]...[payload]             seq: this client's counter
 *   answer    [data]                                  status OK: the router's output; ERROR: the error record; seq: the request's
 *   stream    [connector][payload]                    seq: the connector's frame counter
 *   control   [op][data]                              HELLO (Hello), PING, NO_SESSION
 *
 * The same frame travels on every link; the BLE link only fragments it (transport/framing.ts).
 */

export const HEADER_BYTES = 5

export interface Request {
  /** The keys after the header: the root key of a router, then the keys inside it (an op). `defs.keys` has the root keys by name. */
  readonly keys: readonly number[]
  readonly payload?: Uint8Array
  /** Run on the urgent task, next to (not behind) normal frames. */
  readonly urgent?: boolean
  /** No answer: a failure goes to the errors stream only. */
  readonly noReply?: boolean
}

export interface FrameHeader {
  readonly kind: number
  /** A request's or a stream's flags (`router_flag`). */
  readonly flags: number
  /** An answer's status bit set: ERROR. */
  readonly error: boolean
  readonly dst: number
  readonly src: number
  readonly seq: number
}

export interface Frame extends FrameHeader {
  readonly body: Uint8Array
}

/** One frame: the header, then `body`. */
export const encodeFrame = (defs: LinkDefinitions, header: { kind: number; flags?: number; dst: number; src: number; seq: number }, body: Uint8Array): Uint8Array => {
  if (!Number.isInteger(header.seq) || header.seq < 0 || header.seq > 0xffff) throw new RangeError(`Sequence ${header.seq} is outside 0..65535.`)
  const frame = new Uint8Array(HEADER_BYTES + body.length)
  const kindShift = Math.log2(defs.session.frame.kind & -defs.session.frame.kind)
  frame[0] = ((header.kind << kindShift) & defs.session.frame.kind) | ((header.flags ?? 0) & defs.session.frame.flags)
  frame[1] = header.dst
  frame[2] = header.src
  frame[3] = header.seq & 0xff
  frame[4] = header.seq >> 8
  frame.set(body, HEADER_BYTES)
  return frame
}

/** A frame's header and body; undefined for bytes too short to be one. */
export const decodeFrame = (defs: LinkDefinitions, frame: Uint8Array): Frame | undefined => {
  if (frame.length < HEADER_BYTES) return undefined
  const byte = frame[0]!
  const kindShift = Math.log2(defs.session.frame.kind & -defs.session.frame.kind)
  return {
    kind: (byte & defs.session.frame.kind) >> kindShift,
    flags: byte & defs.session.frame.flags,
    error: (byte & defs.session.frame.status) !== 0,
    dst: frame[1]!,
    src: frame[2]!,
    seq: frame[3]! | (frame[4]! << 8),
    body: frame.subarray(HEADER_BYTES),
  }
}

/** The request frame for `seq`, from `src` (this client's id) to `dst` (the board). */
export const encodeRequest = (defs: LinkDefinitions, seq: number, request: Request, address: { readonly dst: number; readonly src: number }): Uint8Array => {
  if (request.keys.length === 0) throw new RangeError('A request needs at least its root key.')
  const payload = request.payload ?? new Uint8Array(0)
  const body = new Uint8Array(request.keys.length + payload.length)
  body.set(request.keys, 0)
  body.set(payload, request.keys.length)
  const flags = (request.urgent ? defs.flags.urgent : 0) | (request.noReply ? defs.flags.noReply : 0)
  return encodeFrame(defs, { kind: defs.session.kind.request, flags, ...address, seq }, body)
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

/* ---- HELLO (sys_session_hello_t, 24 bytes without children) ---- */

export const HELLO_BYTES = 24

export interface Hello {
  readonly version: number
  readonly boardId: number
  /** The board's answer to a client: the id it gave it. */
  readonly yourId: number
  readonly bootId: number
  /** The board ends the session after this much silence: send something every third of it. */
  readonly timeoutMs: number
}

/** A client's hello: no board id, no MAC; `timeoutMs` how long the client waits for the board (it never ends the session itself). */
export const encodeHello = (defs: LinkDefinitions, timeoutMs: number): Uint8Array => {
  const body = new Uint8Array(1 + HELLO_BYTES)
  const view = new DataView(body.buffer)
  body[0] = defs.session.control.hello
  body[1] = defs.session.version
  body[2] = defs.session.id.none
  body[3] = defs.session.id.none
  view.setUint16(1 + 21, Math.min(timeoutMs, 0xffff), true)
  return body
}

/** A control frame's HELLO; undefined when the body is not one. */
export const decodeHello = (defs: LinkDefinitions, body: Uint8Array): Hello | undefined => {
  if (body.length < 1 + HELLO_BYTES || body[0] !== defs.session.control.hello) return undefined
  const view = new DataView(body.buffer, body.byteOffset, body.byteLength)
  return { version: body[1]!, boardId: body[2]!, yourId: body[3]!, bootId: view.getUint32(1 + 9, true), timeoutMs: view.getUint16(1 + 21, true) }
}
