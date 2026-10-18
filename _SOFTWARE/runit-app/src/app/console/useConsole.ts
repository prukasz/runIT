import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from 'react'
import { CLOCK_SET_AFTER_MS, ErrorInterpreter, ErrorLog, refWithin, Services } from '@backend'
import type { ErrorEntry, ErrorFilter, ErrorReport, LinkState, Ref, RunitLink } from '@backend'
import type { LogLevel } from '@ui/components'

/*
 * The console: what the board says on its own, read only (no commands go from here).
 *
 *   errors   the errors stream (every chain the board reports, a failed command's too), kept in an `ErrorLog`: the same error
 *            again counts up, what was read is marked. Each entry is read again with the app's current names (a device renamed
 *            after the error came shows its new name), and is filed under what it is about: a device, a pin of it, a settings
 *            tab (the router key of its command), else the board.
 *   logs     the logs stream: one ESP_LOG line per frame (`I (854) tag: text`), cut to its level, its tag and its text.
 *   app      the link coming up and going down, so a gap in the lines has its reason.
 *
 * Time: when the link comes up the app gives the board the time (`SYS TIME_SET`), reads its clock back (`TIME_GET`: the clock and the
 * uptime of one moment) and reads the errors the board kept (`LOG ERRORS`, the latest ones, also those from before the link was up: an
 * error of the start). An error is dated by the board's clock in its record, a log line by its uptime (`I (854) tag`) and the board's
 * clock minus its uptime; so each shows when it happened on the board, in UTC, not when it reached the app.
 */

export interface ConsoleLine {
  readonly id: number
  readonly at: number
  readonly level: LogLevel
  /** The ESP_LOG tag (the file or module), or none for the app's own lines. */
  readonly source?: string
  readonly text: string
  /** The board's uptime of the line (ESP_LOG's milliseconds). */
  readonly uptimeMs?: number
}

const LINES_KEPT = 1000
/** The board keeps its errors in RAM: read at most this many (the firmware's CONFIG_SYS_LOG_ERRORS_KEPT is at most 64). */
const HISTORY_MAX = 64
const ESP_LINE = /^([EWIDV]) \((\d+)\) ([^:]+): ?([\s\S]*)$/

/** One line of the logs stream, cut into its parts (a line that is not ESP_LOG's keeps its text as it is). */
export const parseLogLine = (text: string): Pick<ConsoleLine, 'level' | 'source' | 'text' | 'uptimeMs'> => {
  const m = ESP_LINE.exec(text.replace(/\s+$/, ''))
  return m ? { level: m[1] as LogLevel, uptimeMs: Number(m[2]), source: m[3]!, text: m[4]! } : { level: 'I', text: text.replace(/\s+$/, '') }
}

const decoder = new TextDecoder()

export function useConsole(link: RunitLink, state: LinkState, interpreter: ErrorInterpreter) {
  // The log keeps the records; they are read for display with `interpreter` (the app's names now).
  const log = useMemo(() => new ErrorLog(new ErrorInterpreter({ services: interpreter.services }), { capacity: 300 }), [link])
  const [entries, setEntries] = useState<readonly ErrorEntry[]>([])
  const [lines, setLines] = useState<readonly ConsoleLine[]>([])
  const nextId = useRef(1)
  // The board's clock minus its uptime (ms), once known: what dates a line from its uptime.
  const offset = useRef<number | undefined>(undefined)
  const dated = (uptimeMs: number | undefined): number => (uptimeMs !== undefined && offset.current !== undefined ? uptimeMs + offset.current : Date.now())
  const push = useCallback((line: Omit<ConsoleLine, 'id' | 'at'>) => {
    const made: ConsoleLine = { ...line, id: nextId.current++, at: dated(line.uptimeMs) }
    setLines((all) => (all.length >= LINES_KEPT ? [...all.slice(all.length - LINES_KEPT + 1), made] : [...all, made]))
  }, [])

  useEffect(() => {
    const offChange = log.onChange(setEntries)
    const offErrors = log.attach(link)
    const offLogs = link.on('logs', (data) => push(parseLogLine(decoder.decode(data))))
    return () => { offChange(); offErrors(); offLogs(); log.close() }
  }, [log, link, push])

  // Each time the link comes up: the board gets the time, tells its clock, and gives the errors it kept. A firmware without these commands
  // refuses them: the lines and errors then carry the time they arrived.
  useEffect(() => {
    offset.current = undefined  // a restarted board has another clock
    if (state !== 'connected') return
    let alive = true
    const services = new Services(link)
    void (async () => {
      try {
        await services.call('SYS', 'TIME_SET', { unix_ms: Date.now() }, { timeoutMs: 1500, label: 'SYS TIME_SET' })
        const { value } = await services.call('SYS', 'TIME_GET', undefined, { timeoutMs: 1500, label: 'SYS TIME_GET' })
        const clock = value as { unix_ms: number; uptime_ms: number }
        if (!alive || Number(clock.unix_ms) < CLOCK_SET_AFTER_MS) return
        offset.current = Number(clock.unix_ms) - Number(clock.uptime_ms)
        log.setClock(Number(clock.unix_ms), Number(clock.uptime_ms))
        // the lines that came before the clock was known are dated now
        setLines((all) => all.map((l) => (l.uptimeMs === undefined ? l : { ...l, at: l.uptimeMs + offset.current! })))
      } catch { /* a firmware without the clock */ }
      try {
        for (let index = 0, total = 1; alive && index < Math.min(total, HISTORY_MAX); index++) {
          const { data } = await services.call('LOG', 'ERRORS', { index }, { timeoutMs: 1500, label: 'LOG ERRORS' })
          total = data[0] ?? 0
          if (data.length > 1) log.addBytes(data.subarray(1))
        }
      } catch { /* a firmware without the history, or one that is gone */ }
    })()
    return () => { alive = false }
  }, [link, state, log])

  // The link's changes, as the app's own lines.
  const last = useRef<LinkState>(state)
  useEffect(() => {
    const was = last.current
    last.current = state
    if (was === state) return
    const text = state === 'connected' ? 'Connected to the board.' : state === 'reconnecting' ? 'The link dropped: reconnecting…' : state === 'idle' && was !== 'connecting' ? 'Disconnected.' : undefined
    if (text) push({ level: 'app', text })
  }, [state, push])

  const read = useCallback((entry: ErrorEntry): ErrorReport => interpreter.interpret(entry.report.record), [interpreter])

  return {
    entries, lines, interpreter,
    /** The entry read with the app's current names. */
    read,
    unread: (ref?: Ref | string) => entries.filter((e) => !e.read && (ref === undefined || matchesRef(e, ref))).length,
    about: (ref: Ref | string) => entries.filter((e) => matchesRef(e, ref)),
    /**
     * The entries about what the app's resolver makes of a reference (`interpreter.target`: a variable, a block of the program on
     * the board): `match` takes that target. A variable has no ref of its own on the board, only the object the program gave it.
     */
    aboutTarget: (match: (target: unknown) => boolean) => entries.filter((e) => e.report.refs.some((r) => match(interpreter.target(r)))),
    markRead: (filter?: ErrorFilter) => void log.markRead(filter),
    clear: (filter?: ErrorFilter) => void log.clear(filter),
    clearLines: () => setLines([]),
  }
}

const matchesRef = (entry: ErrorEntry, ref: Ref | string): boolean => entry.report.refs.some((r) => refWithin(r, ref))

export type ConsoleState = ReturnType<typeof useConsole>

/** Where a reference leads in the app: the device (or its pin) in Devices, a settings tab in Project. */
export interface ConsoleJumps {
  /** The name and the jump of a reference the app can show, else undefined (shown as text only). */
  target: (ref: Ref) => { name: string; detail?: string; open?: () => void; kind: 'device' | 'settings' | 'other' } | undefined
}

export const ConsoleContext = createContext<{ console: ConsoleState; jumps: ConsoleJumps } | undefined>(undefined)

/** The console's errors and the jumps, for any screen that shows the errors of what it holds. */
export const useErrors = () => useContext(ConsoleContext)
