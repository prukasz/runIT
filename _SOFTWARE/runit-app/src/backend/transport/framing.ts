import type { LinkDefinitions } from '../definitions'

/*
 * Framing of the runIT characteristics (SYS_BLE.MD "Framing"): every packet starts with one header byte,
 * `bit 7 LAST · bit 6 FIRST · bits 0-5 packet counter (mod 64, 0 at FIRST)`, then a piece of the frame. A frame that fits one
 * packet is `C0 <frame>`. The bits come from the firmware (`ble_frag` in enums.json).
 */

/** The packets of a frame; each is at most `packetBytes` long, header included. */
export const fragment = (defs: LinkDefinitions, frame: Uint8Array, packetBytes: number): Uint8Array[] => {
  if (!Number.isInteger(packetBytes) || packetBytes < 2) throw new RangeError(`A packet of ${packetBytes} bytes cannot carry data.`)
  if (frame.length === 0) throw new RangeError('An empty frame has no packets.')
  const piece = packetBytes - 1
  const count = Math.ceil(frame.length / piece)
  const { first, last, countMask } = defs.frag
  const packets: Uint8Array[] = []
  for (let i = 0; i < count; i++) {
    const packet = new Uint8Array(1 + Math.min(piece, frame.length - i * piece))
    packet[0] = (i === 0 ? first : 0) | (i === count - 1 ? last : 0) | (i & countMask)
    packet.set(frame.subarray(i * piece, i * piece + packet.length - 1), 1)
    packets.push(packet)
  }
  return packets
}

export type Reassembled = { readonly frame: Uint8Array } | { readonly dropped: string } | undefined

/**
 * Puts the packets of one characteristic back together, as the firmware does: FIRST starts a frame, a packet out of order or
 * outside a frame drops the rest of the frame up to the next FIRST, a frame over `frameMax` is dropped, LAST delivers.
 * Returns undefined while a frame is still arriving.
 */
export class Reassembler {
  private readonly parts: Uint8Array[] = []
  private length = 0
  private expected = 0
  private active = false

  constructor(
    private readonly defs: LinkDefinitions,
    private readonly frameMax: number = defs.frameMax,
  ) {}

  push(packet: Uint8Array): Reassembled {
    if (packet.length < 1) return undefined
    const { first, last, countMask } = this.defs.frag
    const header = packet[0]!
    const count = header & countMask
    if (header & first) {
      this.parts.length = 0
      this.length = 0
      this.expected = 0
      this.active = true
    }
    if (!this.active) return { dropped: `packet ${count} outside a frame` }
    if (count !== this.expected) {
      this.active = false
      return { dropped: `packet ${count} where ${this.expected} was expected` }
    }
    if (this.length + packet.length - 1 > this.frameMax) {
      this.active = false
      return { dropped: `frame over ${this.frameMax} bytes` }
    }
    this.parts.push(packet.subarray(1))
    this.length += packet.length - 1
    this.expected = (this.expected + 1) & countMask
    if (!(header & last)) return undefined
    const frame = new Uint8Array(this.length)
    let at = 0
    for (const part of this.parts) {
      frame.set(part, at)
      at += part.length
    }
    this.reset()
    return { frame }
  }

  /** Forgets a frame in progress (the link dropped). */
  reset(): void {
    this.parts.length = 0
    this.length = 0
    this.expected = 0
    this.active = false
  }
}
