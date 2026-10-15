import type { LinkDefinitions } from '../definitions'

/*
 * The router's frames (ROUTER.MD "Wire format"):
 *
 *   request   [seq][flags][root key][key]...[payload]
 *   response  [stream = response][seq][status][data]      status OK: data = the router's output; ERROR: data = error record
 *
 * The same frame shape travels on every link; the BLE link only fragments it (transport/framing.ts).
 */

export interface Request {
  /** The keys after the flags: the root key of a router, then the keys inside it (an op). `defs.keys` has the root keys by name. */
  readonly keys: readonly number[]
  readonly payload?: Uint8Array
  /** Run on the urgent task, next to (not behind) normal frames. */
  readonly urgent?: boolean
}

/** The request frame for `seq`. */
export const encodeRequest = (defs: LinkDefinitions, seq: number, request: Request): Uint8Array => {
  if (!Number.isInteger(seq) || seq < 0 || seq > 0xff) throw new RangeError(`Sequence byte ${seq} is outside 0..255.`)
  if (request.keys.length === 0) throw new RangeError('A request needs at least its root key.')
  const payload = request.payload ?? new Uint8Array(0)
  const frame = new Uint8Array(2 + request.keys.length + payload.length)
  frame[0] = seq
  frame[1] = request.urgent ? defs.flags.urgent : 0
  frame.set(request.keys, 2)
  frame.set(payload, 2 + request.keys.length)
  return frame
}

export interface Response {
  readonly seq: number
  readonly ok: boolean
  /** The router's output, or the error record when `ok` is false. */
  readonly data: Uint8Array
}

/** The response in a frame of the response stream; undefined for any other stream, or a frame too short to be one. */
export const decodeResponse = (defs: LinkDefinitions, frame: Uint8Array): Response | undefined => {
  if (frame.length < 3 || frame[0] !== defs.streams.response) return undefined
  return { seq: frame[1]!, ok: frame[2] === defs.status.ok, data: frame.subarray(3) }
}
