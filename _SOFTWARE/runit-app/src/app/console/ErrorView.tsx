import { Check, X } from 'lucide-react'
import { bundledEnums, refKey } from '@backend'
import type { ErrorEntry as Entry, Ref } from '@backend'
import { Button, Card, ErrorEntry, RefPill } from '@ui/components'
import { useErrors } from './useConsole'
import type { ConsoleJumps, ConsoleState } from './useConsole'

const severities = bundledEnums().err_sev_e?.members ?? []
/** A severity's name as errors.json gives it (`High`). */
export const severityName = (severity: number): string => severities.find((m) => m.value === severity)?.alias ?? `Severity ${severity}`

/** A time of day, to the second. */
export const clock = (at: number): string => new Date(at).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })

/** The references worth a pill: the ones the app can show, a pin in place of its device, a device in place of a settings tab. */
const pillRefs = (refs: readonly Ref[], jumps: ConsoleJumps) => {
  const shown = refs.flatMap((ref) => {
    const target = jumps.target(ref)
    return target ? [{ ref, target }] : []
  })
  const devices = shown.filter((s) => s.target.kind === 'device')
  return shown.filter((s) => {
    if (s.ref.kind === 'device') return !devices.some((d) => d.ref.scope && refKey(d.ref.scope) === refKey(s.ref))
    if (s.target.kind === 'settings') return devices.length === 0
    return true
  }).filter((s, i, all) => all.findIndex((o) => refKey(o.ref) === refKey(s.ref)) === i)
}

/** One error of the log, read with the app's names: what went wrong, where, what it is about (pills that open it). */
export function ErrorView({ entry, state, jumps, active, onClick }: { entry: Entry; state: ConsoleState; jumps: ConsoleJumps; active?: boolean; onClick?: () => void }) {
  const report = state.read(entry)
  const pills = pillRefs(report.refs, jumps)
  return (
    <ErrorEntry
      message={report.message} severity={report.severity} severityLabel={severityName(report.severity)} hint={report.cause?.hint}
      where={report.where} command={report.command?.label} count={entry.count} time={clock(entry.last)} unread={!entry.read} active={active}
      chain={report.chain.map((n) => ({ name: n.name, message: n.message, context: n.context, ...(n.description ? { description: n.description } : {}) }))}
      refs={pills.length > 0 ? pills.map(({ ref, target }) => (
        <RefPill key={refKey(ref)} compact name={target.name} detail={target.detail} refKey={refKey(ref)} onJump={target.open} />
      )) : undefined}
      actions={(
        <>
          {!entry.read && <Button size="icon-sm" aria-label="Mark as read" title="Mark as read" onClick={() => state.markRead({ ids: [entry.id] })}><Check aria-hidden="true" /></Button>}
          <Button size="icon-sm" aria-label="Remove" title="Remove from the log" onClick={() => state.clear({ ids: [entry.id] })}><X aria-hidden="true" /></Button>
        </>
      )}
      onClick={onClick ?? (entry.read ? undefined : () => state.markRead({ ids: [entry.id] }))}
    />
  )
}

/**
 * The errors about one thing (a device, a pin, a settings tab), as a card on its screen. Nothing when there are none: the card
 * comes when the board reports one. `about` is the board's ref keys; `match` the app's target instead (a variable, a block:
 * `ConsoleState.aboutTarget`).
 */
export function ErrorsCard({ about = [], match, title = 'Errors' }: { about?: string | readonly string[]; match?: (target: unknown) => boolean; title?: string }) {
  const errors = useErrors()
  if (!errors) return null
  const keys = typeof about === 'string' ? [about] : about
  const matched = match ? errors.console.aboutTarget(match) : []
  const entries = errors.console.entries.filter((e) => matched.includes(e) || keys.some((k) => errors.console.about(k).includes(e)))
  if (entries.length === 0) return null
  const ids = entries.map((e) => e.id)
  return (
    <Card title={title} className="console-errors-card" subhead="What the board reported. A click marks one read; the console has them all."
      actions={(
        <span className="console-card-actions">
          {entries.some((e) => !e.read) && <Button size="sm" onClick={() => errors.console.markRead({ ids })}><Check aria-hidden="true" />Mark read</Button>}
          <Button size="sm" onClick={() => errors.console.clear({ ids })}><X aria-hidden="true" />Clear</Button>
        </span>
      )}>
      <div className="console-errors">
        {entries.map((e) => <ErrorView key={e.id} entry={e} state={errors.console} jumps={errors.jumps} />)}
      </div>
    </Card>
  )
}
