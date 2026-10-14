/** Little-endian byte building, as every VM record is packed (`__packed`, `vm_wire.h`). */
export class ByteWriter {
  private buffer = new Uint8Array(64)
  private view = new DataView(this.buffer.buffer)
  private length = 0

  get size(): number {
    return this.length
  }

  private reserve(more: number): void {
    const need = this.length + more
    if (need <= this.buffer.length) return
    let capacity = this.buffer.length
    while (capacity < need) capacity *= 2
    const grown = new Uint8Array(capacity)
    grown.set(this.buffer.subarray(0, this.length))
    this.buffer = grown
    this.view = new DataView(grown.buffer)
  }

  u8(value: number): this {
    this.reserve(1)
    this.view.setUint8(this.length, value)
    this.length += 1
    return this
  }

  u16(value: number): this {
    this.reserve(2)
    this.view.setUint16(this.length, value, true)
    this.length += 2
    return this
  }

  u32(value: number): this {
    this.reserve(4)
    this.view.setUint32(this.length, value, true)
    this.length += 4
    return this
  }

  i32(value: number): this {
    this.reserve(4)
    this.view.setInt32(this.length, value, true)
    this.length += 4
    return this
  }

  f32(value: number): this {
    this.reserve(4)
    this.view.setFloat32(this.length, value, true)
    this.length += 4
    return this
  }

  bytes(value: Uint8Array): this {
    this.reserve(value.length)
    this.buffer.set(value, this.length)
    this.length += value.length
    return this
  }

  /** The text as UTF-8 bytes; the caller has checked the length it needs. */
  text(value: string): this {
    return this.bytes(new TextEncoder().encode(value))
  }

  finish(): Uint8Array {
    return this.buffer.slice(0, this.length)
  }
}

export const concat = (...parts: readonly Uint8Array[]): Uint8Array => {
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0))
  let at = 0
  for (const part of parts) {
    out.set(part, at)
    at += part.length
  }
  return out
}

export const toHex = (bytes: Uint8Array): string => Array.from(bytes, (b) => b.toString(16).padStart(2, '0')).join('')
