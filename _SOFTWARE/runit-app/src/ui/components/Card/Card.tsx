import type { ReactNode } from 'react'
import './Card.css'

/**
 * A titled panel surface for a group of settings. `icon` puts an amber icon before the title; `subhead` explains it; `actions` sit
 * at the right of the title; `help` is its text in help mode (HelpOverlay).
 */
export function Card({ title, icon, subhead, actions, help, className = '', children }: { title?: ReactNode; icon?: ReactNode; subhead?: ReactNode; actions?: ReactNode; help?: string; className?: string; children?: ReactNode }) {
  const heading = title && (icon ? <div className="ui-card-header">{icon}<h3>{title}</h3></div> : <h3>{title}</h3>)
  return (
    <div className={`ui-card ${className}`.trim()} data-help={help}>
      {actions ? <div className="ui-card-title">{heading}{actions}</div> : heading}
      {subhead && <p className="ui-card-subhead">{subhead}</p>}
      {children}
    </div>
  )
}

/** The cards of one editor page, stacked. */
export function CardStack({ className = '', children }: { className?: string; children?: ReactNode }) {
  return <div className={`ui-card-stack ${className}`.trim()}>{children}</div>
}
