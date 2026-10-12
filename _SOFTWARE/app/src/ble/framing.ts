/**
 * Fragment header of the runIT characteristics (0xFFE1 TX, 0xFFE2 RX, 0xFFE3 LOGS), see components/sys/sys_ble/SYS_BLE.MD.
 * Every packet starts with one byte: bit 7 LAST, bit 6 FIRST, bits 0-5 a counter (mod 64, 0 at FIRST).
 * A frame that fits one packet is `C0 <frame>`.
 */
export const FRAG_LAST = 0x80
export const FRAG_FIRST = 0x40
export const FRAG_COUNT_MASK = 0x3f

/** Longest frame the board takes (CONFIG_ROUTER_FRAME_MAX / CONFIG_SYS_BLE_TX_ITEM_MAX). */
export const FRAME_MAX = 512

/** Cuts a frame into packets of at most `packetSize` bytes, header included. */
export const splitFrame = (frame: Uint8Array, packetSize: number): Uint8Array[] => {
  if (packetSize < 2) throw new RangeError('A packet needs room for the header and one byte.')
  if (frame.length > FRAME_MAX) throw new RangeError(`A frame is at most ${FRAME_MAX} bytes (got ${frame.length}).`)
  const body = packetSize - 1
  const count = Math.max(1, Math.ceil(frame.length / body))
  const packets: Uint8Array[] = []
  for (let i = 0; i < count; i++) {
    const part = frame.subarray(i * body, (i + 1) * body)
    const packet = new Uint8Array(part.length + 1)
    packet[0] = (i === 0 ? FRAG_FIRST : 0) | (i === count - 1 ? FRAG_LAST : 0) | (i & FRAG_COUNT_MASK)
    packet.set(part, 1)
    packets.push(packet)
  }
  return packets
}

/** Why a packet was dropped; the frame it belonged to is lost up to the next FIRST. */
export type FramingProblem = 'empty' | 'out-of-frame' | 'bad-counter' | 'too-long'

/** Joins the packets of one characteristic back into frames. One instance per characteristic and connection. */
export class Reassembler {
  private buffer: number[] = []
  private expected = 0
  private inFrame = false

  private readonly onProblem?: (problem: FramingProblem) => void

  constructor(onProblem?: (problem: FramingProblem) => void) {
    this.onProblem = onProblem
  }

  /** Returns the whole frame when `packet` completes one. */
  push(packet: Uint8Array): Uint8Array | undefined {
    if (packet.length === 0) return this.drop('empty')
    const header = packet[0]
    if (header & FRAG_FIRST) {
      this.buffer = []
      this.expected = 0
      this.inFrame = true
    } else if (!this.inFrame) {
      return this.drop('out-of-frame')
    }
    if ((header & FRAG_COUNT_MASK) !== this.expected) return this.drop('bad-counter')
    if (this.buffer.length + packet.length - 1 > FRAME_MAX) return this.drop('too-long')
    for (let i = 1; i < packet.length; i++) this.buffer.push(packet[i])
    this.expected = (this.expected + 1) & FRAG_COUNT_MASK
    if (!(header & FRAG_LAST)) return undefined
    this.inFrame = false
    const frame = Uint8Array.from(this.buffer)
    this.buffer = []
    return frame
  }

  reset(): void {
    this.buffer = []
    this.expected = 0
    this.inFrame = false
  }

  private drop(problem: FramingProblem): undefined {
    this.reset()
    this.onProblem?.(problem)
    return undefined
  }
}
