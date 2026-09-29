import type { ReactNode } from 'react'
import './PanelHeader.css'

/** Title row at the top of a details panel: optional leading icon, the title, then badges or actions. */
export function PanelHeader({ icon, title, className = '', children }: { icon?: ReactNode; title: ReactNode; className?: string; children?: ReactNode }) {
  return (
    <div className={`panel-header ${className}`.trim()}>
      {icon}
      <h2>{title}</h2>
      {children}
    </div>
  )
}
