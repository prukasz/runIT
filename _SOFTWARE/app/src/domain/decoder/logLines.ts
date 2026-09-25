/** ESP-IDF log levels, by the letter that starts a log line (`E (1234) tag: text`). */
export type LogLevel = 'error' | 'warn' | 'info' | 'debug' | 'verbose'

const LEVEL_LETTERS: Readonly<Record<string, LogLevel>> = { E: 'error', W: 'warn', I: 'info', D: 'debug', V: 'verbose' }

export type LogEntry =
  /** An ESP-IDF log line (log format v1). */
  | { readonly kind: 'esp'; readonly level: LogLevel; readonly timestampMs: number; readonly tag: string; readonly text: string }
  /**
   * One node of an error chain as sys_errors prints it
   * (`[depth] owner=NAME (0xOOOO) tag=NAME (id): description`); the same chain
   * also arrives in binary on the errors stream.
   */
  | { readonly kind: 'error-chain'; readonly depth: number; readonly owner: string; readonly ownerId: number; readonly tag: string; readonly tagId: number; readonly text: string }
  /** Anything else (`printf` output, a line cut to the link's frame size …). */
  | { readonly kind: 'text'; readonly text: string }

// Colour escapes (CONFIG_LOG_COLORS): matching ESC is the point.
// eslint-disable-next-line no-control-regex
const ANSI = /\x1b\[[0-9;]*m/g
const ESP_LINE = /^([EWIDV]) \((\d+)\) ([^:]+): ?(.*)$/
const CHAIN_LINE = /^\[(\d+)\] owner=(\S+) \(0x([0-9A-Fa-f]+)\) tag=(\S+) \((-?\d+)\): ?(.*)$/

export const parseLogLine = (line: string): LogEntry => {
  const clean = line.replace(ANSI, '').replace(/\r$/, '')
  const esp = ESP_LINE.exec(clean)
  if (esp) return { kind: 'esp', level: LEVEL_LETTERS[esp[1]], timestampMs: Number(esp[2]), tag: esp[3], text: esp[4] }
  const chain = CHAIN_LINE.exec(clean)
  if (chain) {
    return { kind: 'error-chain', depth: Number(chain[1]), owner: chain[2], ownerId: Number.parseInt(chain[3], 16), tag: chain[4], tagId: Number(chain[5]), text: chain[6] }
  }
  return { kind: 'text', text: clean }
}

/** Lines of one logs-stream frame (stream byte already removed). Empty lines are dropped. */
export const decodeLogFrame = (body: Uint8Array): LogEntry[] =>
  new TextDecoder()
    .decode(body)
    .split('\n')
    .filter((line) => line.replace(ANSI, '').trim() !== '')
    .map(parseLogLine)
