type PrintfScalar = number | bigint | string

/** A value with a name, printed as the value and then ` (name)`: `device 12 (INA3221)`. */
export interface LabeledValue {
  readonly value: PrintfScalar
  readonly label: string
}

export type PrintfValue = PrintfScalar | LabeledValue

const SPEC = /%([-+ #0]*)(\d+)?(?:\.(\d*))?(hh|h|ll|l|z|j|t|L)?([diouxXeEfFgGcsp%])/g

const toBigInt = (value: PrintfScalar): bigint => {
  if (typeof value === 'bigint') return value
  if (typeof value === 'number') return BigInt(Math.trunc(value))
  return BigInt(Number.parseInt(value, 10) || 0)
}

const toNumber = (value: PrintfScalar): number => (typeof value === 'string' ? Number(value) : Number(value))

/** C's %e exponent has at least two digits: 1.5e+03, not 1.5e+3. */
const cExponent = (text: string): string => text.replace(/e([+-])(\d)$/, 'e$10$2')

const formatG = (value: number, precision: number, alternate: boolean): string => {
  if (!Number.isFinite(value)) return String(value)
  const digits = precision === 0 ? 1 : precision
  if (value === 0) return alternate ? (0).toFixed(digits - 1) : '0'
  const exponent = Math.floor(Math.log10(Math.abs(Number(value.toExponential(digits - 1)))))
  let text = digits > exponent && exponent >= -4 ? value.toFixed(digits - 1 - exponent) : cExponent(value.toExponential(digits - 1))
  if (!alternate) {
    text = text.includes('e')
      ? text.replace(/\.?0+e/, 'e')
      : text.includes('.') ? text.replace(/\.?0+$/, '') : text
  }
  return text
}

/**
 * Format like C's snprintf for the conversions the firmware's messages use
 * (d i u o x X c s f F e E g G p %, flags - + space # 0, width, precision;
 * length modifiers are accepted and ignored). A missing argument prints '?'.
 * A LabeledValue prints its value, then ` (label)`.
 */
export const formatPrintf = (format: string, args: readonly PrintfValue[]): string => {
  let next = 0
  return format.replace(SPEC, (_match, flags: string, width: string | undefined, precisionText: string | undefined, _length: string | undefined, conversion: string) => {
    if (conversion === '%') return '%'
    if (next >= args.length) return '?'
    const arg = args[next++]
    const value = typeof arg === 'object' ? arg.value : arg
    const suffix = typeof arg === 'object' ? ` (${arg.label})` : ''
    const left = flags.includes('-')
    const zero = flags.includes('0') && !left
    const plus = flags.includes('+')
    const space = flags.includes(' ')
    const alternate = flags.includes('#')
    const precision = precisionText === undefined ? undefined : Number(precisionText || 0)
    let sign = ''
    let body: string

    switch (conversion) {
      case 'd':
      case 'i': {
        const integer = toBigInt(value)
        if (integer < 0n) sign = '-'
        else if (plus) sign = '+'
        else if (space) sign = ' '
        body = (integer < 0n ? -integer : integer).toString()
        if (precision !== undefined) body = body.padStart(precision, '0')
        break
      }
      case 'u':
      case 'o':
      case 'x':
      case 'X':
      case 'p': {
        let integer = toBigInt(value)
        if (integer < 0n) integer += 1n << 32n // C prints a negative 32-bit value as its unsigned bit pattern
        const radix = conversion === 'u' ? 10 : conversion === 'o' ? 8 : 16
        body = integer.toString(radix)
        if (conversion === 'X') body = body.toUpperCase()
        if (precision !== undefined) body = body.padStart(precision, '0')
        if ((alternate && integer !== 0n && (conversion === 'x' || conversion === 'X')) || conversion === 'p') sign = conversion === 'X' ? '0X' : '0x'
        break
      }
      case 'c':
        body = typeof value === 'string' ? value.charAt(0) : String.fromCharCode(Number(value))
        break
      case 's':
        body = String(value)
        if (precision !== undefined) body = body.slice(0, precision)
        break
      default: {
        const number = toNumber(value)
        if (number < 0 || Object.is(number, -0)) sign = '-'
        else if (plus) sign = '+'
        else if (space) sign = ' '
        const magnitude = Math.abs(number)
        if (conversion === 'f' || conversion === 'F') body = magnitude.toFixed(precision ?? 6)
        else if (conversion === 'e' || conversion === 'E') body = cExponent(magnitude.toExponential(precision ?? 6))
        else body = formatG(magnitude, precision ?? 6, alternate)
        if (conversion === 'E' || conversion === 'G' || conversion === 'F') body = body.toUpperCase()
      }
    }

    const length = sign.length + body.length
    const target = width === undefined ? 0 : Number(width)
    if (length >= target) return sign + body + suffix
    if (left) return (sign + body).padEnd(target, ' ') + suffix
    // C ignores the 0 flag for text, and for integers that have a precision.
    const zeroPads = zero && conversion !== 's' && conversion !== 'c' && !(precision !== undefined && 'diouxXp'.includes(conversion))
    if (zeroPads) return sign + body.padStart(target - sign.length, '0') + suffix
    return (sign + body).padStart(target, ' ') + suffix
  })
}
