import type { LinkDefinitions } from '../definitions'

/*
 * A framed characteristic (SYS_BLE.MD "Framing"): every packet starts with one header byte, `bit 7 LAST · bit 6 FIRST · bits 0-5
 * the packet counter (mod 64, 0 at FIRST)`, then a piece of the frame. A frame that fits one packet is `C0 <frame>`. The bits are
 * the firmware's (`ble_frag`). A raw characteristic carries a whole item per notification and needs none of this.
 */

/** The packets of a frame; each is at most `packetBytes` long, its header byte included. */
export const fragment = (defs: LinkDefinitions, frame: Uint8Array, packetBytes: number): Uint8Array[] => {
  if (!Number.isInteger(packetBytes) || packetBytes < 2) throw new RangeError(`A packet of ${packetBytes} bytes cannot carry data.`)
  if (frame.length === 0) throw new RangeError('An empty frame has no packets.')
  const { first, last, countMask } = defs.frag
  const piece = packetBytes - 1
  const count = Math.ceil(frame.length / piece)
  return Array.from({ length: count }, (_, i) => {
    const part = frame.subarray(i * piece, (i + 1) * piece)
    const packet = new Uint8Array(1 + part.length)
    packet[0] = (i === 0 ? first : 0) | (i === count - 1 ? last : 0) | (i & countMask)
    packet.set(part, 1)
    return packet
  })
}

export type Reassembled = { readonly frame: Uint8Array } | { readonly dropped: string } | undefined

/**
 * Joins the packets of one characteristic as the firmware does: FIRST starts a frame, a packet out of order or outside a frame
 * drops the rest of it up to the next FIRST, a frame over `frameMax` is dropped, LAST delivers it. Undefined while a frame is
 * still arriving.
 */
export class Reassembler {
  private parts: Uint8Array[] = []
  private length = 0
  private expected = 0
  private active = false

  constructor(private readonly defs: LinkDefinitions) {}

  push(packet: Uint8Array): Reassembled {
    if (packet.length < 1) return undefined
    const { first, last, countMask } = this.defs.frag
    const head = packet[0]!
    const count = head & countMask
    if (head & first) {
      this.reset()
      this.active = true
    }
    if (!this.active) return { dropped: `packet ${count} outside a frame` }
    if (count !== this.expected) {
      const dropped = `packet ${count} where ${this.expected} was expected`
      this.reset()
      return { dropped }
    }
    if (this.length + packet.length - 1 > this.defs.frameMax) {
      this.reset()
      return { dropped: `a frame over ${this.defs.frameMax} bytes` }
    }
    this.parts.push(packet.slice(1))
    this.length += packet.length - 1
    this.expected = (this.expected + 1) & countMask
    if (!(head & last)) return undefined
    const frame = new Uint8Array(this.length)
    let at = 0
    for (const part of this.parts) {
      frame.set(part, at)
      at += part.length
    }
    this.reset()
    return { frame }
  }

  /** Forgets a frame on its way (the link dropped). */
  reset(): void {
    this.parts = []
    this.length = 0
    this.expected = 0
    this.active = false
  }
}
