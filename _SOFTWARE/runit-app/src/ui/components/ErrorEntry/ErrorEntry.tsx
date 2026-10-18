import type { ReactNode } from 'react'
import { Badge } from '../Badge/Badge'
import type { BadgeTone } from '../Badge/Badge'
import './ErrorEntry.css'

/** A step of an error's chain, outermost first: where it went (context) down to what went wrong. */
export interface ErrorStep {
  readonly name: string
  readonly message: string
  readonly description?: string
  readonly context: boolean
}

/** The badge tone of an error severity (`err_severity_e`: 0 none … 4 critical). */
export const severityTone = (severity: number): BadgeTone => (severity >= 3 ? 'danger' : severity === 2 ? 'warning' : severity === 1 ? 'info' : 'neutral')

/**
 * One error as a user reads it (the error log of the UI kit): the severity, what went wrong, what to do about it, where (the context steps), what it
 * is about (`refs`: pills that jump there), the command that failed, and the whole chain behind a disclosure. An unread entry has
 * the red edge; `count` is how many times it happened.
 */
export function ErrorEntry({ message, severity, severityLabel, hint, where = [], refs, command, chain = [], count = 1, time, unread = false, active = false, actions, onClick }: {
  message: string
  severity: number
  /** The severity's name (`High`). */
  severityLabel?: string
  /** What the user can do about it. */
  hint?: string
  where?: readonly string[]
  refs?: ReactNode
  command?: string
  chain?: readonly ErrorStep[]
  count?: number
  time?: string
  unread?: boolean
  active?: boolean
  actions?: ReactNode
  onClick?: () => void
}) {
  const classes = ['err-entry', unread && 'is-unread', active && 'is-active', onClick && 'is-clickable'].filter(Boolean).join(' ')
  return (
    <div className={classes} onClick={onClick}>
      <div className="err-head">
        <Badge tone={severityTone(severity)} size="compact" filled={unread}>{severityLabel ?? `Severity ${severity}`}</Badge>
        {count > 1 && <Badge tone="count" size="compact" title={`Happened ${count} times`}>×{count}</Badge>}
        {time && <span className="err-time">{time}</span>}
        {actions && <span className="err-actions" onClick={(event) => event.stopPropagation()}>{actions}</span>}
      </div>
      <div className="err-message">{message}</div>
      {hint && <div className="err-hint">{hint}</div>}
      {where.length > 0 && <div className="err-where">{where.map((w, i) => <Badge key={i} size="compact">{w.replace(/^In /, '')}</Badge>)}</div>}
      {refs && <div className="err-refs">{refs}</div>}
      {(chain.length > 0 || command) && (
        <details className="err-chain" onClick={(event) => event.stopPropagation()}>
          <summary>{command ? `${command} · ` : ''}{chain.length} {chain.length === 1 ? 'step' : 'steps'}</summary>
          {chain.map((step, i) => (
            <div key={i} className={step.context ? 'is-context' : ''} title={step.description}>
              <span className="err-step-name">{step.name}</span> {step.message}
            </div>
          ))}
        </details>
      )}
    </div>
  )
}
