/*
 * The serial terminal's pure part: what is typed as the bytes that go out (a GPS's command, a LoRa module's AT line) and what comes back
 * as text to read.
 */

export type SerialFormat = 'text' | 'hex'

/** What a typed text line ends with: most modules want one. */
export const LINE_ENDINGS = [
  { value: 'crlf', label: 'CR LF', bytes: [0x0d, 0x0a] },
  { value: 'lf', label: 'LF', bytes: [0x0a] },
  { value: 'cr', label: 'CR', bytes: [0x0d] },
  { value: 'none', label: 'Nothing', bytes: [] },
] as const

export type LineEnding = (typeof LINE_ENDINGS)[number]['value']

/** Hex bytes as typed: `AA 0b`, `aa0b`, `0xAA, 0x0B`. Throws on anything else. */
export const parseHex = (text: string): Uint8Array => {
  const digits = text.replace(/0x/gi, '').replace(/[\s,]/g, '')
  if (!/^[0-9a-f]*$/i.test(digits)) throw new RangeError('Hex takes only the digits 0-9 and A-F.')
  if (digits.length % 2 !== 0) throw new RangeError('Hex takes two digits per byte.')
  return Uint8Array.from(digits.match(/../g) ?? [], (b) => parseInt(b, 16))
}

/** The bytes of what was typed: a text line with its ending, or hex as it is. */
export const encodeInput = (text: string, format: SerialFormat, ending: LineEnding): Uint8Array => {
  if (format === 'hex') return parseHex(text)
  const line = new TextEncoder().encode(text)
  const end = LINE_ENDINGS.find((e) => e.value === ending)!.bytes
  const out = new Uint8Array(line.length + end.length)
  out.set(line)
  out.set(end, line.length)
  return out
}

const hexOf = (data: Uint8Array): string => [...data].map((b) => b.toString(16).toUpperCase().padStart(2, '0')).join(' ')

/** Bytes to read: as text when all of them are printable (a line's ending left out), else as hex. */
export const showBytes = (data: Uint8Array): string => {
  const printable = data.every((b) => (b >= 0x20 && b < 0x7f) || b === 0x09 || b === 0x0a || b === 0x0d)
  return printable ? new TextDecoder().decode(data).replace(/[\r\n]+$/, '') : hexOf(data)
}
