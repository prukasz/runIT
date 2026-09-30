import { useEffect, useMemo, useState } from 'react'
import { SelectField, TextField } from './components/FormField'
import type { RunitBleSession } from './backend/runitBleSession'
import { decodeBoardFrame, errorOwnerName, errorTagName } from './domain/decoder'
import type { ErrorNodeReport, ErrorReport, LogEntry, LogLevel } from './domain/decoder'
import { runitErrorCatalog, runitStreamCatalog, runitValueNames } from './domain/descriptors'
import type { ErrorCatalog } from './domain/descriptors'
import { ConsoleLinkedText, ConsoleValue } from './ConsoleReference'
import type { ResolveConsoleReference } from './ConsoleReference'

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
const LEVEL_STYLES = ['none', 'info', 'warning', 'high', 'critical']

const LOG_LEVELS: readonly LogLevel[] = ['error', 'warn', 'info', 'debug', 'verbose']
const clock = (at: number): string => new Date(at).toLocaleTimeString(undefined, { hour12: false }) + `.${String(at % 1000).padStart(3, '0')}`

function LevelBadge({ catalog, node }: { readonly catalog: ErrorCatalog; readonly node: ErrorNodeReport }) {
  if (!node.level) return <span className="console-level is-none">?</span>
  const index = Math.max(0, catalog.levels.indexOf(node.level))
  return <span className={`console-level is-${LEVEL_STYLES[Math.min(index, LEVEL_STYLES.length - 1)]}`}>{node.level.alias}</span>
}

/** Payload fields with their names: `dev_id 12 INA3221 · pin_num 4`. */
function FieldList({ node, resolveReference }: { readonly node: ErrorNodeReport; readonly resolveReference?: ResolveConsoleReference }) {
  const entries = Object.entries(node.fields).filter(([name]) => name !== 'unused')
  if (entries.length === 0) return null
  return (
    <div className="console-error-fields">
      {entries.map(([name, value]) => (
        <span key={name}>
          {name} <span className="console-field-value">{Array.isArray(value) ? `[${value.join(', ')}]` : typeof value === 'number' ? <ConsoleValue field={name} value={value} siblings={node.fields} resolveReference={resolveReference} /> : String(value)}</span>
          {node.labels[name] !== undefined && <span className="console-field-decoded"> {node.labels[name]}</span>}
        </span>
      ))}
    </div>
  )
}

function ErrorNodeLine({ catalog, node, index, resolveReference }: { readonly catalog: ErrorCatalog; readonly node: ErrorNodeReport; readonly index: number; readonly resolveReference?: ResolveConsoleReference }) {
  return (
    <li className="console-error-node">
      <div className="console-inline">
        <span className="console-muted">[{index}]</span>
        <LevelBadge catalog={catalog} node={node} />
        <span>{errorTagName(catalog, node.tagId)}</span>
        <span className="console-muted">@ {errorOwnerName(catalog, node.ownerId)}</span>
      </div>
      {node.message && <div className="console-error-message"><ConsoleLinkedText text={node.message} resolveReference={resolveReference} /></div>}
      <FieldList node={node} resolveReference={resolveReference} />
      {node.payloadMismatch && <div className="console-warning">payload is {node.payload.byteLength} bytes, the catalog expects {node.tag?.payloadSize}</div>}
    </li>
  )
}

function ErrorItem({ catalog, item, resolveReference }: { readonly catalog: ErrorCatalog; readonly item: ReceivedError; readonly resolveReference?: ResolveConsoleReference }) {
  const { report } = item
  const root = report.nodes.at(-1)
  const worst = report.nodes.reduce<ErrorNodeReport | undefined>((best, node) => (node.level && (!best?.level || node.level.value > best.level.value) ? node : best), undefined)
  return (
    <details className="console-error-item">
      <summary>
        <span className="console-inline">
          <span className="console-muted">{clock(item.at)}</span>
          {worst && <LevelBadge catalog={catalog} node={worst} />}
          <span>{root ? errorTagName(catalog, root.tagId) : 'empty chain'}</span>
          {root?.message && <span><ConsoleLinkedText text={root.message} resolveReference={resolveReference} /></span>}
          {report.nodes.length > 1 && <span className="console-muted">({report.nodes.length} nodes)</span>}
        </span>
      </summary>
      <ol className="console-error-chain">
        {report.nodes.map((node, index) => <ErrorNodeLine key={index} catalog={catalog} node={node} index={index} resolveReference={resolveReference} />)}
      </ol>
      <p className="console-muted">
        Outermost first, root cause last.
        {report.truncated && ` Cut to fit the frame: ${report.nodeCount} of ${report.depth} nodes.`}
        {report.corrupt && ' The board marked this chain corrupt or over-depth.'}
        {report.malformed && ` Malformed: ${report.malformed}.`}
      </p>
    </details>
  )
}

function LogLine({ item, resolveReference }: { readonly item: ReceivedLog; readonly resolveReference?: ResolveConsoleReference }) {
  const { entry } = item
  if (entry.kind === 'esp') {
    return (
      <div className={`console-log-line is-${entry.level}`}>
        <span className="console-muted">{String(entry.timestampMs).padStart(8, ' ')} </span>
        {entry.level.charAt(0).toUpperCase()} <span className="console-muted">{entry.tag}:</span> <ConsoleLinkedText text={entry.text} resolveReference={resolveReference} />
      </div>
    )
  }
  if (entry.kind === 'error-chain') return <div className="console-log-line is-error">[{entry.depth}] {entry.tag} @ {entry.owner}: <ConsoleLinkedText text={entry.text} resolveReference={resolveReference} /></div>
  return <div className="console-log-line"><ConsoleLinkedText text={entry.text} resolveReference={resolveReference} /></div>
}

interface Props {
  readonly session?: RunitBleSession
  readonly resolveReference?: ResolveConsoleReference
}

/** Test-console panels for the board's errors stream (binary chains) and logs stream (text). */
export default function DiagnosticsConsole({ session, resolveReference }: Props) {
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
    <div className="console-stack">
      <section className="console-section">
        <div className="console-heading">
          <h2>Errors ({errors.length})</h2>
          <button className="console-button" onClick={() => setErrors([])}>Clear</button>
          <span className="console-muted">stream 0x{catalogs.errors.stream.toString(16).padStart(2, '0')} · catalog schema {hex32(catalogs.errors.schemaId)}</span>
        </div>
        {firmwareSchema === catalogs.errors.schemaId && <p className="console-success">The board's error maps match the app's catalog.</p>}
        {firmwareSchema !== undefined && firmwareSchema !== catalogs.errors.schemaId && (
          <p className="console-warning">
            The board's error maps differ from the app's catalog (board {hex32(firmwareSchema)}, app {hex32(catalogs.errors.schemaId)}). Known tags still decode;
            regenerate with <code>python data-structures/auto-annotations/errors/generate-errors.py</code> for the firmware you flashed.
          </p>
        )}
        <div className="console-error-list">
          {errors.length === 0 ? <p className="console-muted">{session ? 'No errors yet.' : 'Open a command session to receive errors.'}</p> : errors.map((item) => <ErrorItem key={item.id} catalog={catalogs.errors} item={item} resolveReference={resolveReference} />)}
        </div>
      </section>

      <section className="console-section">
        <div className="console-heading">
          <h2>Logs ({visibleLogs.length}/{logs.length})</h2>
          <button className="console-button" onClick={() => setLogs([])}>Clear</button>
          <label className="console-inline-label">
            Level
            <SelectField value={minLevel} onChange={(event) => setMinLevel(event.target.value as LogLevel)}>
              {LOG_LEVELS.map((level) => <option key={level} value={level}>{level} and above</option>)}
            </SelectField>
          </label>
          <label className="console-inline-label">
            <TextField type="checkbox" checked={showChainLines} onChange={(event) => setShowChainLines(event.target.checked)} />
            Error-chain lines
          </label>
          <label className="console-inline-label">
            <TextField type="checkbox" checked={showText} onChange={(event) => setShowText(event.target.checked)} />
            Other text
          </label>
          <TextField aria-label="Filter logs" className="console-search" value={search} onChange={(event) => setSearch(event.target.value)} placeholder="filter" />
        </div>
        <div className="console-log">
          {visibleLogs.length === 0 ? <p className="console-muted">{session ? 'No log lines yet.' : 'Open a command session to receive logs.'}</p> : visibleLogs.map((item) => <LogLine key={item.id} item={item} resolveReference={resolveReference} />)}
        </div>
      </section>
    </div>
  )
}
