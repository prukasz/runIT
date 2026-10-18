import { useLayoutEffect, useRef } from 'react'
import { CheckCheck, CircuitBoard, Cpu, ScrollText, SlidersHorizontal, Trash2 } from 'lucide-react'
import { refKey } from '@backend'
import type { ErrorEntry } from '@backend'
import { Badge, Button, DeviceTile, LogLine, PanelHeader, SegmentedSwitch, TreeSlab } from '@ui/components'
import { clock, ErrorView } from './ErrorView'
import type { ConsoleJumps, ConsoleLine, ConsoleState } from './useConsole'

/*
 * The Console view: the board's logs and errors as they come, read only. The left panel files the errors under what they are
 * about (a device, a settings tab, the board); a choice there shows only those. The screen is the feed, newest at the bottom.
 */

/** What the feed shows. */
export type ConsoleShow = 'all' | 'errors' | 'logs'
export const CONSOLE_SHOWS = ['all', 'errors', 'logs'] as const

/** The key of what an error is about, its name and kind: its first device, else a settings tab, else the board. */
export interface Subject { readonly key: string; readonly name: string; readonly kind: 'device' | 'settings' | 'board' }

export const BOARD_SUBJECT: Subject = { key: 'board', name: 'The board', kind: 'board' }

export const subjectOf = (entry: ErrorEntry, jumps: ConsoleJumps): Subject => {
  for (const kind of ['device', 'settings'] as const) {
    for (const ref of entry.report.refs) {
      const own = ref.kind === 'device' ? ref : ref.scope?.kind === 'device' ? ref.scope : ref
      const target = jumps.target(own)
      if (target?.kind === kind) return { key: refKey(own), name: target.name, kind }
    }
  }
  return BOARD_SUBJECT
}

const SUBJECT_ICON = { device: Cpu, settings: SlidersHorizontal, board: CircuitBoard } as const

/** The left panel: everything, then each thing the errors are about, with how many are unread. */
export function ConsoleExplorer({ state, jumps, subject, onSubject }: { state: ConsoleState; jumps: ConsoleJumps; subject: string | undefined; onSubject: (key: string | undefined) => void }) {
  const subjects = new Map<string, { subject: Subject; total: number; unread: number }>()
  for (const e of state.entries) {
    const s = subjectOf(e, jumps)
    const known = subjects.get(s.key) ?? { subject: s, total: 0, unread: 0 }
    subjects.set(s.key, { ...known, total: known.total + 1, unread: known.unread + (e.read ? 0 : 1) })
  }
  const unread = state.entries.filter((e) => !e.read).length
  return (
    <div className="console-explorer">
      <PanelHeader title="Console" />
      <p className="devices-hint">The board's logs and errors as they come. Read only: commands go from the other views.</p>
      <TreeSlab icon={<DeviceTile size="small" icon={ScrollText} />} label="Everything" selected={subject === undefined} onClick={() => onSubject(undefined)}
        badges={unread > 0 ? <Badge tone="danger" size="compact" title="Errors not read yet">{unread}</Badge> : undefined} />
      {[...subjects.values()].map(({ subject: s, total, unread: n }) => (
        <TreeSlab key={s.key} icon={<DeviceTile size="small" icon={SUBJECT_ICON[s.kind]} />} label={s.name} selected={subject === s.key} isError={n > 0} onClick={() => onSubject(s.key)}
          badges={<Badge tone={n > 0 ? 'danger' : 'neutral'} size="compact" title={n > 0 ? `${n} of ${total} not read yet` : `${total} read`}>{n > 0 ? n : total}</Badge>} />
      ))}
      {subjects.size === 0 && <p className="devices-hint">No errors.</p>}
    </div>
  )
}

/** The log tag of the board's error trace (`trace_errors`, SYS_LOG.MD): the same chain an error entry shows read, so All leaves it to Logs. */
const ERROR_TRACE = 'err_log.c'

type FeedItem = { kind: 'line'; at: number; line: ConsoleLine } | { kind: 'error'; at: number; entry: ErrorEntry }

/** The feed: the lines and the errors in the order they came (an error that came again moves down with its count). */
export function ConsoleScreen({ state, jumps, show, onShow, subject, onSubject }: {
  state: ConsoleState; jumps: ConsoleJumps; show: ConsoleShow; onShow: (show: ConsoleShow) => void; subject: string | undefined; onSubject: (key: string | undefined) => void
}) {
  const entries = state.entries.filter((e) => subject === undefined || subjectOf(e, jumps).key === subject)
  const items: FeedItem[] = [
    ...(show === 'errors' || subject !== undefined ? [] : state.lines.filter((line) => show === 'logs' || line.source !== ERROR_TRACE).map((line): FeedItem => ({ kind: 'line', at: line.at, line }))),
    ...(show === 'logs' ? [] : entries.map((entry): FeedItem => ({ kind: 'error', at: entry.last, entry }))),
  ].sort((a, b) => a.at - b.at)

  // Follows the newest while the feed is scrolled to its end; a scroll up to read stays where it is.
  const feed = useRef<HTMLDivElement>(null)
  const atEnd = useRef(true)
  useLayoutEffect(() => {
    const el = feed.current
    if (el && atEnd.current) el.scrollTop = el.scrollHeight
  }, [items.length, items.at(-1)?.at])
  const subjectName = subject && (subject === BOARD_SUBJECT.key ? BOARD_SUBJECT.name : entries[0] ? subjectOf(entries[0], jumps).name : undefined)
  const unread = entries.filter((e) => !e.read).map((e) => e.id)

  return (
    <div className="console-screen">
      <div className="console-bar">
        <SegmentedSwitch label="What the console shows" value={show} options={[['all', 'All'], ['errors', 'Errors'], ['logs', 'Logs']]} onChange={onShow} />
        {subjectName && <Button size="sm" title="Show everything again" onClick={() => onSubject(undefined)}>Errors of {subjectName} ✕</Button>}
        <span className="console-bar-spacer" />
        <Button size="sm" disabled={unread.length === 0} onClick={() => state.markRead({ ids: unread })}><CheckCheck aria-hidden="true" />Mark read</Button>
        <Button size="sm" title="Remove what is shown" onClick={() => { if (show !== 'logs') state.clear({ ids: entries.map((e) => e.id) }); if (show !== 'errors' && subject === undefined) state.clearLines() }}><Trash2 aria-hidden="true" />Clear</Button>
      </div>
      <div className="console-feed" ref={feed} onScroll={(event) => { const el = event.currentTarget; atEnd.current = el.scrollHeight - el.scrollTop - el.clientHeight < 24 }}>
        {items.length === 0 && <p className="devices-hint">{show === 'logs' ? 'No log lines yet. The board sends them while it is connected and Logging sends them (Project, Logging settings).' : 'Nothing yet: what the board reports appears here.'}</p>}
        {items.map((item) => item.kind === 'line'
          ? <LogLine key={`l${item.line.id}`} time={clock(item.line.at)} level={item.line.level} source={item.line.source} text={item.line.text} />
          : <div key={`e${item.entry.id}`} className="console-error"><ErrorView entry={item.entry} state={state} jumps={jumps} /></div>)}
      </div>
    </div>
  )
}
