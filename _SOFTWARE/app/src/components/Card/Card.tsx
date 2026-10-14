import type { ReactNode } from 'react'
import './Card.css'

/** A titled panel surface for a group of settings. `icon` puts an amber icon before the title; `subhead` explains it. */
export function Card({ title, icon, subhead, className = '', children }: { title?: ReactNode; icon?: ReactNode; subhead?: ReactNode; className?: string; children?: ReactNode }) {
  return (
    <div className={`ui-card ${className}`.trim()}>
      {title && (icon ? <div className="ui-card-header">{icon}<h3>{title}</h3></div> : <h3>{title}</h3>)}
      {subhead && <p className="ui-card-subhead">{subhead}</p>}
      {children}
    </div>
  )
}

/** The cards of one editor page, stacked. */
export function CardStack({ className = '', children }: { className?: string; children?: ReactNode }) {
  return <div className={`ui-card-stack ${className}`.trim()}>{children}</div>
}
