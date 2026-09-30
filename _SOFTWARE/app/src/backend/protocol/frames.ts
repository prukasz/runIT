/** Logical routes the app sends and receives runIT data on (app-side names, not firmware IDs). */
export const RUNIT_ROUTE = {
  /** Commands to the board (`[seq][class][packet][payload]`). */
  interface: 'runit.interface',
  /** Board → app notifications, one route per notify characteristic (`runit.notify.0xFFE1`). */
  notifyPrefix: 'runit.notify.',
} as const

/**
 * The firmware's command-response envelope. The values come from the
 * generated descriptors (domain/descriptors → runitInterfaceProtocol()):
 * the response stream byte from contracts `response_stream`, the OK value
 * from its `status_enum` in enums.json.
 */
export interface InterfaceProtocol {
  /** First byte of every response frame. */
  readonly responseStream: number
  /** Status byte of a successful command. */
  readonly statusOk: number
}

/** Root cause of a failed command: `u16 tag, u16 owner` (little-endian). */
export interface InterfaceErrorData {
  readonly tag: number
  readonly owner: number
}

/** One decoded `[stream][seq][class][packet][status][data]` frame. */
export interface InterfaceResponseFrame {
  readonly seq: number
  readonly requestClass: number
  readonly requestPacket: number
  readonly status: number
  readonly data: Uint8Array
  /** Present when the status is not OK and the data carries tag + owner. */
  readonly error?: InterfaceErrorData
}

const RESPONSE_HEADER_LENGTH = 5

/** Undefined when the frame is not an interface response (another stream, or too short). */
export const decodeInterfaceResponse = (frame: Uint8Array, protocol: InterfaceProtocol): InterfaceResponseFrame | undefined => {
  if (frame.byteLength < RESPONSE_HEADER_LENGTH || frame[0] !== protocol.responseStream) return undefined
  const data = frame.slice(RESPONSE_HEADER_LENGTH)
  const status = frame[4]
  let error: InterfaceErrorData | undefined
  if (status !== protocol.statusOk && data.byteLength >= 4) {
    const view = new DataView(data.buffer, data.byteOffset, data.byteLength)
    error = { tag: view.getUint16(0, true), owner: view.getUint16(2, true) }
  }
  return { seq: frame[1], requestClass: frame[2], requestPacket: frame[3], status, data, error }
}

/** Prefix a command body `[class][packet][payload]` with its sequence byte. */
export const encodeCommandFrame = (seq: number, body: Uint8Array): Uint8Array => {
  if (!Number.isInteger(seq) || seq < 0 || seq > 0xff) throw new RangeError(`Sequence byte ${seq} is outside 0..255.`)
  const frame = new Uint8Array(body.byteLength + 1)
  frame[0] = seq
  frame.set(body, 1)
  return frame
}
