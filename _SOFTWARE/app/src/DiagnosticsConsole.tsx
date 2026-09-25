import { useEffect, useMemo, useState } from 'react'
import type { RunitBleSession } from './backend/runitBleSession'
import { decodeBoardFrame, errorOwnerName, errorTagName } from './domain/decoder'
import type { ErrorNodeReport, ErrorReport, LogEntry, LogLevel } from './domain/decoder'
import { runitErrorCatalog, runitStreamCatalog, runitValueNames } from './domain/descriptors'
import type { ErrorCatalog } from './domain/descriptors'

interface ReceivedError {
  readonly id: number
  readonly at: number
  readonly report: ErrorReport
}

interface ReceivedLog {
  readonly id: number
  readonly at: number
  readonly entry: LogEntry
}

/** Keys for list rows; kept across sessions so entries from an earlier connection never collide. */
let nextId = 0

const MAX_ERRORS = 100
const MAX_LOGS = 500

/** Severity colours by position in the published levels (NONE … CRITICAL). */
const LEVEL_STYLES = ['bg-slate-700 text-slate-200', 'bg-sky-900 text-sky-200', 'bg-amber-900 text-amber-200', 'bg-orange-800 text-orange-100', 'bg-red-800 text-red-100']

const LOG_LEVELS: readonly LogLevel[] = ['error', 'warn', 'info', 'debug', 'verbose']
const LOG_STYLES: Readonly<Record<LogLevel, string>> = {
  error: 'text-red-300', warn: 'text-amber-300', info: 'text-emerald-300', debug: 'text-sky-300', verbose: 'text-slate-400',
}

const clock = (at: number): string => new Date(at).toLocaleTimeString(undefined, { hour12: false }) + `.${String(at % 1000).padStart(3, '0')}`

function LevelBadge({ catalog, node }: { readonly catalog: ErrorCatalog; readonly node: ErrorNodeReport }) {
  if (!node.level) return <span className="rounded bg-slate-800 px-1 text-slate-400">?</span>
  const index = Math.max(0, catalog.levels.indexOf(node.level))
  return <span className={`rounded px-1 ${LEVEL_STYLES[Math.min(index, LEVEL_STYLES.length - 1)]}`}>{node.level.alias}</span>
}

/** Payload fields with their names: `dev_id 12 INA3221 · pin_num 4`. */
function FieldList({ node }: { readonly node: ErrorNodeReport }) {
  const entries = Object.entries(node.fields).filter(([name]) => name !== 'unused')
  if (entries.length === 0) return null
  return (
    <div className="flex flex-wrap gap-x-3 pl-6 text-slate-500">
      {entries.map(([name, value]) => (
        <span key={name}>
          {name} <span className="text-slate-300">{Array.isArray(value) ? `[${value.join(', ')}]` : String(value)}</span>
          {node.labels[name] !== undefined && <span className="text-sky-300"> {node.labels[name]}</span>}
        </span>
      ))}
    </div>
  )
}

function ErrorNodeLine({ catalog, node, index }: { readonly catalog: ErrorCatalog; readonly node: ErrorNodeReport; readonly index: number }) {
  return (
    <li className="space-y-0.5">
      <div className="flex flex-wrap items-baseline gap-2">
        <span className="text-slate-500">[{index}]</span>
        <LevelBadge catalog={catalog} node={node} />
        <span className="text-slate-100">{errorTagName(catalog, node.tagId)}</span>
        <span className="text-slate-500">@ {errorOwnerName(catalog, node.ownerId)}</span>
      </div>
      {node.message && <div className="pl-6 text-slate-300">{node.message}</div>}
      <FieldList node={node} />
      {node.payloadMismatch && <div className="pl-6 text-amber-300">payload is {node.payload.byteLength} bytes, the catalog expects {node.tag?.payloadSize}</div>}
    </li>
  )
}

function ErrorItem({ catalog, item }: { readonly catalog: ErrorCatalog; readonly item: ReceivedError }) {
  const { report } = item
  const root = report.nodes.at(-1)
  const worst = report.nodes.reduce<ErrorNodeReport | undefined>((best, node) => (node.level && (!best?.level || node.level.value > best.level.value) ? node : best), undefined)
  return (
    <details className="rounded border border-slate-800 bg-slate-950 p-2">
      <summary className="cursor-pointer list-none">
        <span className="flex flex-wrap items-baseline gap-2">
          <span className="text-slate-500">{clock(item.at)}</span>
          {worst && <LevelBadge catalog={catalog} node={worst} />}
          <span className="text-slate-100">{root ? errorTagName(catalog, root.tagId) : 'empty chain'}</span>
          {root?.message && <span className="text-slate-300">{root.message}</span>}
          {report.nodes.length > 1 && <span className="text-slate-500">({report.nodes.length} nodes)</span>}
        </span>
      </summary>
      <ol className="mt-2 space-y-1 border-t border-slate-800 pt-2 text-xs">
        {report.nodes.map((node, index) => <ErrorNodeLine key={index} catalog={catalog} node={node} index={index} />)}
      </ol>
      <p className="mt-1 text-xs text-slate-500">
        Outermost first, root cause last.
        {report.truncated && ` Cut to fit the frame: ${report.nodeCount} of ${report.depth} nodes.`}
        {report.corrupt && ' The board marked this chain corrupt or over-depth.'}
        {report.malformed && ` Malformed: ${report.malformed}.`}
      </p>
    </details>
  )
}

function LogLine({ item }: { readonly item: ReceivedLog }) {
  const { entry } = item
  if (entry.kind === 'esp') {
    return (
      <div className={LOG_STYLES[entry.level]}>
        <span className="text-slate-600">{String(entry.timestampMs).padStart(8, ' ')} </span>
        {entry.level.charAt(0).toUpperCase()} <span className="text-slate-400">{entry.tag}:</span> {entry.text}
      </div>
    )
  }
  if (entry.kind === 'error-chain') return <div className="text-red-300/80">  [{entry.depth}] {entry.tag} @ {entry.owner}: {entry.text}</div>
  return <div className="text-slate-400">{entry.text}</div>
}

interface Props {
  readonly session?: RunitBleSession
}

/** Test-console panels for the board's errors stream (binary chains) and logs stream (text). */
export default function DiagnosticsConsole({ session }: Props) {
  const catalogs = useMemo(() => ({ streams: runitStreamCatalog(), errors: runitErrorCatalog(), names: runitValueNames() }), [])
  const [errors, setErrors] = useState<ReceivedError[]>([])
  const [logs, setLogs] = useState<ReceivedLog[]>([])
  const [firmwareSchema, setFirmwareSchema] = useState<number>()
  const [minLevel, setMinLevel] = useState<LogLevel>('verbose')
  const [showChainLines, setShowChainLines] = useState(false)
  const [showText, setShowText] = useState(true)
  const [search, setSearch] = useState('')

  useEffect(() => {
    if (!session) return undefined
    return session.received.subscribe((frame) => {
      const decoded = decodeBoardFrame(frame.data, catalogs)
      if (decoded.kind === 'errors') {
        const item = { id: nextId++, at: frame.receivedAt, report: decoded.report }
        setErrors((entries) => [item, ...entries].slice(0, MAX_ERRORS))
        if (!decoded.report.malformed || decoded.report.nodeCount > 0) setFirmwareSchema(decoded.report.schemaId)
      } else if (decoded.kind === 'logs') {
        const items = decoded.entries.map((entry) => ({ id: nextId++, at: frame.receivedAt, entry }))
        setLogs((entries) => [...items.reverse(), ...entries].slice(0, MAX_LOGS))
      }
    })
  }, [session, catalogs])

  const visibleLogs = useMemo(() => {
    const limit = LOG_LEVELS.indexOf(minLevel)
    const needle = search.trim().toLowerCase()
    return logs.filter(({ entry }) => {
      if (entry.kind === 'esp' && LOG_LEVELS.indexOf(entry.level) > limit) return false
      if (entry.kind === 'error-chain' && !showChainLines) return false
      if (entry.kind === 'text' && !showText) return false
      if (!needle) return true
      const haystack = entry.kind === 'esp' ? `${entry.tag} ${entry.text}` : entry.kind === 'error-chain' ? `${entry.tag} ${entry.owner} ${entry.text}` : entry.text
      return haystack.toLowerCase().includes(needle)
    })
  }, [logs, minLevel, showChainLines, showText, search])

  const hex32 = (value: number): string => `0x${value.toString(16).toUpperCase().padStart(8, '0')}`

  return (
    <>
      <section className="rounded border border-slate-700 bg-slate-900 p-4">
        <div className="flex flex-wrap items-center gap-2">
          <h2 className="font-semibold text-white">Errors ({errors.length})</h2>
          <button onClick={() => setErrors([])}>Clear</button>
          <span className="text-xs text-slate-500">stream 0x{catalogs.errors.stream.toString(16).padStart(2, '0')} · catalog schema {hex32(catalogs.errors.schemaId)}</span>
        </div>
        {firmwareSchema === catalogs.errors.schemaId && <p className="mt-2 text-xs text-emerald-400">The board's error maps match the app's catalog.</p>}
        {firmwareSchema !== undefined && firmwareSchema !== catalogs.errors.schemaId && (
          <p className="mt-2 rounded border border-amber-700 bg-amber-950 p-2 text-xs text-amber-200">
            The board's error maps differ from the app's catalog (board {hex32(firmwareSchema)}, app {hex32(catalogs.errors.schemaId)}). Known tags still decode;
            regenerate with <code>python data-structures/auto-annotations/errors/generate-errors.py</code> for the firmware you flashed.
          </p>
        )}
        <div className="mt-3 max-h-96 space-y-2 overflow-auto">
          {errors.length === 0 ? <p className="text-slate-500">{session ? 'No errors yet.' : 'Open a command session to receive errors.'}</p> : errors.map((item) => <ErrorItem key={item.id} catalog={catalogs.errors} item={item} />)}
        </div>
      </section>

      <section className="rounded border border-slate-700 bg-slate-900 p-4">
        <div className="flex flex-wrap items-center gap-2">
          <h2 className="font-semibold text-white">Logs ({visibleLogs.length}/{logs.length})</h2>
          <button onClick={() => setLogs([])}>Clear</button>
          <label className="flex items-center gap-1 text-xs text-slate-400">
            Level
            <select value={minLevel} onChange={(event) => setMinLevel(event.target.value as LogLevel)}>
              {LOG_LEVELS.map((level) => <option key={level} value={level}>{level} and above</option>)}
            </select>
          </label>
          <label className="flex items-center gap-1 text-xs text-slate-400">
            <input type="checkbox" checked={showChainLines} onChange={(event) => setShowChainLines(event.target.checked)} />
            Error-chain lines
          </label>
          <label className="flex items-center gap-1 text-xs text-slate-400">
            <input type="checkbox" checked={showText} onChange={(event) => setShowText(event.target.checked)} />
            Other text
          </label>
          <input aria-label="Filter logs" className="w-40" value={search} onChange={(event) => setSearch(event.target.value)} placeholder="filter" />
        </div>
        <div className="mt-3 max-h-96 overflow-auto whitespace-pre-wrap text-xs">
          {visibleLogs.length === 0 ? <p className="text-slate-500">{session ? 'No log lines yet.' : 'Open a command session to receive logs.'}</p> : visibleLogs.map((item) => <LogLine key={item.id} item={item} />)}
        </div>
      </section>
    </>
  )
}
