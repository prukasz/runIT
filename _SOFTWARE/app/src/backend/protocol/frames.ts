/**
 * The router's wire format (components/core/router/ROUTER.MD) and the stream bytes of the board's outbound streams
 * (components/sys/sys_connector/sys_connector.h).
 *
 *   request   [seq][flags][root key][key]...[payload]
 *   response  [0x05][seq][status][data]        status 0 OK, 1 error: data = error record
 *   stream    [stream byte][payload]
 */
import { enumValue, values } from '../catalog'

export const STREAM = values('stream_byte', {
  telemetry: 'SYS_CONNECTOR_STREAM_TELEMETRY', logs: 'SYS_CONNECTOR_STREAM_LOGS', errors: 'SYS_CONNECTOR_STREAM_ERRORS',
  response: 'SYS_CONNECTOR_STREAM_RESPONSE', events: 'SYS_CONNECTOR_STREAM_EVENTS',
})
export const FLAG_URGENT = enumValue('router_flag', 'ROUTER_FLAG_URGENT')
export const STATUS_OK = enumValue('router_status_e', 'ROUTER_STATUS_OK')
export const STATUS_ERROR = enumValue('router_status_e', 'ROUTER_STATUS_ERROR')
const SRC_COMMAND = enumValue('router_err_src_e', 'ROUTER_ERR_SRC_COMMAND')
const SRC_INTERNAL = enumValue('router_err_src_e', 'ROUTER_ERR_SRC_INTERNAL')
const REQUEST_CUT = enumValue('router_err_flag', 'ROUTER_ERR_REQ_CUT')
/** Longest request the board takes (CONFIG_ROUTER_FRAME_MAX). */
export const REQUEST_MAX = 512

export const concat = (...parts: readonly Uint8Array[]): Uint8Array => {
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0))
  let at = 0
  for (const part of parts) {
    out.set(part, at)
    at += part.length
  }
  return out
}

/** `keys` is the route: the root key, then the keys below it (the op). */
export const buildRequest = (seq: number, keys: readonly number[], payload: Uint8Array = new Uint8Array(), urgent = false): Uint8Array => {
  if (keys.length === 0) throw new RangeError('A request needs at least the root key.')
  const frame = concat(Uint8Array.of(seq & 0xff, urgent ? FLAG_URGENT : 0, ...keys), payload)
  if (frame.length > REQUEST_MAX) throw new RangeError(`A request is at most ${REQUEST_MAX} bytes (got ${frame.length}).`)
  return frame
}

export interface Response {
  readonly seq: number
  readonly status: number
  readonly data: Uint8Array
}

/** Undefined when `frame` is not a response. */
export const parseResponse = (frame: Uint8Array): Response | undefined =>
  frame.length >= 3 && frame[0] === STREAM.response ? { seq: frame[1], status: frame[2], data: frame.subarray(3) } : undefined

export interface ErrorNode {
  /** `module << 16 | code`. */
  readonly tag: number
  readonly module: number
  readonly code: number
  /** The node's effective severity: 0 none, 1 low, 2 medium, 3 high, 4 critical. */
  readonly severity: number
  readonly payload: Uint8Array
}

export interface ErrorRecord {
  /** `command`: raised while routing a request (link, seq and the request are known); `internal`: a task, boot, a device. */
  readonly source: 'internal' | 'command'
  readonly link?: number
  readonly peer?: number
  readonly seq?: number
  readonly flags?: number
  /** The request as the board saw it (keys + payload), cut to what fits. */
  readonly request?: Uint8Array
  readonly requestCut?: boolean
  /** Outermost first; the last node is the root cause. */
  readonly nodes: readonly ErrorNode[]
  /** Nodes that did not fit were left out (the root cause may be missing). */
  readonly chainCut: boolean
}

/** Decodes an error record (the data of an error response, or the payload of an errors stream frame). Throws RangeError when malformed. */
export const decodeErrorRecord = (data: Uint8Array): ErrorRecord => {
  const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
  const need = (n: number, at: number) => {
    if (at + n > data.length) throw new RangeError('The error record is cut short.')
  }
  let at = 0
  need(1, at)
  const src = data[at++]
  let head: Partial<ErrorRecord> = {}
  if (src === SRC_COMMAND) {
    need(8, at)
    const link = data[at]
    const peer = view.getUint32(at + 1, true)
    const seq = data[at + 5]
    const flags = data[at + 6]
    const reqLen = data[at + 7]
    at += 8
    const length = reqLen & ~REQUEST_CUT
    need(length, at)
    head = { link, peer, seq, flags, request: data.subarray(at, at + length), requestCut: (reqLen & REQUEST_CUT) !== 0 }
    at += length
  } else if (src !== SRC_INTERNAL) {
    throw new RangeError(`Unknown error record source ${src}.`)
  }
  need(1, at)
  const count = data[at] & 0x7f
  const chainCut = (data[at] & 0x80) !== 0
  at++
  const nodes: ErrorNode[] = []
  for (let i = 0; i < count; i++) {
    need(6, at)
    const tag = view.getUint32(at, true)
    const severity = data[at + 4]
    const length = data[at + 5]
    at += 6
    need(length, at)
    nodes.push({ tag, module: tag >>> 16, code: tag & 0xffff, severity, payload: data.subarray(at, at + length) })
    at += length
  }
  return { source: src === SRC_COMMAND ? 'command' : 'internal', ...head, nodes, chainCut }
}
