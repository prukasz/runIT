import type { LinkDefinitions } from '../definitions'

/*
 * Every frame on a line and every packet of the board's data matrix starts with the same header (CMD.MD "The header", `cmd_hdr_t`
 * of cmd.h), `CMD_HEAD` bytes, packed, little endian:
 *
 *   [type u8][src u8][key u8][seq u16][data]
 *
 *   type   bits of `cmd_type`: the router's flags (urgent, no reply), an answer's status, the kind (`cmd_kind_e`)
 *   src    who made it (`cmd_src`): a board's id, a client's (this app's own), 0 until a session puts it on a line
 *   key    a module's number: a request's router, an answer's request's, a PUSH's stream, the session's own for CONTROL
 *   seq    a request's sender's counter, echoed by its answer; a PUSH's count of its class on the session; 0 for CONTROL
 *
 *   request   data [dir][op | id + op ...][args]     the router's input after its key (a stored frame is [key][data])
 *   answer    data the router's output, or the error record when the status is ERROR
 *   push      data the stream's own (an error record, a sys_event_t, a watch's entry, a log line)
 *   control   data [op][...]                          HELLO (sys_session_hello_t), PING, NO_SESSION (SYS_SESSION.MD)
 *
 * The numbers are the definitions' (`enums.json`); the field order is cmd.h's, which `protocol.test.ts` holds against it.
 */

export interface FrameHeader {
  readonly kind: number
  /** The router's flags (`router_flag`) of a request or a push. */
  readonly flags: number
  /** An answer's status is ERROR. */
  readonly error: boolean
  readonly src: number
  readonly key: number
  readonly seq: number
}

export interface Frame extends FrameHeader {
  readonly data: Uint8Array
}

/** A request before it has a header: the router's key, then `[dir][...]` (`body`). */
export interface Request {
  readonly key: number
  /** `[dir][op | id + op][args]`: what the router's module takes after its key. */
  readonly body: Uint8Array
  /** Runs on the urgent task, before the normal requests still queued. */
  readonly urgent?: boolean
  /** No answer: a failure goes to the errors stream only. */
  readonly noReply?: boolean
}

const shiftOf = (mask: number): number => Math.log2(mask & -mask)
const byte = (n: number, what: string): number => {
  if (!Number.isInteger(n) || n < 0 || n > 0xff) throw new RangeError(`${what} ${n} is not a byte.`)
  return n
}

/** One frame: the header, then `data`. */
export const encodeFrame = (defs: LinkDefinitions, header: { kind: number; flags?: number; error?: boolean; src: number; key: number; seq: number }, data: Uint8Array): Uint8Array => {
  if (!Number.isInteger(header.seq) || header.seq < 0 || header.seq > 0xffff) throw new RangeError(`Sequence ${header.seq} is outside 0..65535.`)
  const { type } = defs
  const frame = new Uint8Array(defs.frameHead + data.length)
  frame[0] = ((header.kind << shiftOf(type.kind)) & type.kind) | ((header.flags ?? 0) & type.flags) | (header.error ? type.status : 0)
  frame[1] = byte(header.src, 'Source')
  frame[2] = byte(header.key, 'Key')
  frame[3] = header.seq & 0xff
  frame[4] = header.seq >> 8
  frame.set(data, defs.frameHead)
  return frame
}

/** A frame's header and data; undefined for bytes shorter than a header. */
export const decodeFrame = (defs: LinkDefinitions, frame: Uint8Array): Frame | undefined => {
  if (frame.length < defs.frameHead) return undefined
  const { type } = defs
  const head = frame[0]!
  return {
    kind: (head & type.kind) >> shiftOf(type.kind),
    flags: head & type.flags,
    error: (head & type.status) !== 0,
    src: frame[1]!,
    key: frame[2]!,
    seq: frame[3]! | (frame[4]! << 8),
    data: frame.subarray(defs.frameHead),
  }
}

/** The request frame of sender `src` with `seq`. */
export const encodeRequest = (defs: LinkDefinitions, src: number, seq: number, request: Request): Uint8Array => {
  if (request.body.length === 0) throw new RangeError('A request needs at least its direction byte.')
  const flags = (request.urgent ? defs.flags.urgent : 0) | (request.noReply ? defs.flags.noReply : 0)
  return encodeFrame(defs, { kind: defs.kind.request, flags, src, key: request.key, seq }, request.body)
}

/** A request as the router takes it from a record or a watch: `[key][body]`, no header (SYS_ACTIONS.MD "Image", SYS_WATCH.MD). */
export const requestFrame = (request: Pick<Request, 'key' | 'body'>): Uint8Array => {
  const frame = new Uint8Array(1 + request.body.length)
  frame[0] = request.key
  frame.set(request.body, 1)
  return frame
}

/** The request a stored frame `[key][body]` is. */
export const frameRequest = (frame: Uint8Array): Request => {
  if (frame.length < 2) throw new RangeError('A stored frame is its key, then at least its direction byte.')
  return { key: frame[0]!, body: frame.subarray(1) }
}

/* ---- the session's control frames (SYS_SESSION.MD "A session's life") ---- */

/** `sizeof(sys_session_hello_t)`: boot_id u32, build_id u32 (sys_session.h). */
export const HELLO_BYTES = 8

/** Who a peer is: its id is the hello's `src`, a board's place in the project or a client's own pick. */
export interface Hello {
  readonly id: number
  /** Random per boot: another one means the sender restarted, and what it had of the session is gone. */
  readonly bootId: number
  /** The first 4 bytes of the firmware's ELF SHA-256: whether two boards run the same build; 0 from a client. */
  readonly buildId: number
}

/** A control frame of sender `src`: `[op][data]`. */
export const encodeControl = (defs: LinkDefinitions, src: number, op: number, data: Uint8Array = new Uint8Array(0)): Uint8Array => {
  const body = new Uint8Array(1 + data.length)
  body[0] = op
  body.set(data, 1)
  return encodeFrame(defs, { kind: defs.kind.control, src, key: defs.control.key, seq: 0 }, body)
}

/** A client's HELLO: its id as the source, its own boot id, build 0. */
export const encodeHello = (defs: LinkDefinitions, hello: { readonly id: number; readonly bootId: number; readonly buildId?: number }): Uint8Array => {
  const data = new Uint8Array(HELLO_BYTES)
  const view = new DataView(data.buffer)
  view.setUint32(0, hello.bootId >>> 0, true)
  view.setUint32(4, (hello.buildId ?? 0) >>> 0, true)
  return encodeControl(defs, hello.id, defs.control.hello, data)
}

/** The HELLO of a control frame; undefined when the frame is not one. */
export const decodeHello = (defs: LinkDefinitions, frame: Frame): Hello | undefined => {
  if (frame.kind !== defs.kind.control || frame.data[0] !== defs.control.hello || frame.data.length < 1 + HELLO_BYTES) return undefined
  const view = new DataView(frame.data.buffer, frame.data.byteOffset + 1, HELLO_BYTES)
  return { id: frame.src, bootId: view.getUint32(0, true), buildId: view.getUint32(4, true) }
}
