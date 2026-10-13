import type { CSSProperties, ReactNode } from 'react'

/** The app's stable three-column frame. Screens supply their own content. */
export function WorkspaceShell({ className = '', children }: { className?: string; children: ReactNode }) {
  return <main className={`shell ${className}`.trim()}>{children}</main>
}

export function ExplorerPanel({ open, width, children }: { open: boolean; width: number; children: ReactNode }) {
  return <aside className={`side-panel left-panel ${open ? 'is-open' : ''}`} style={open ? { width } : undefined} aria-label="Explorer">{children}</aside>
}

export function MainScreen({ label, toolbar, children }: { label: string; toolbar: ReactNode; children: ReactNode }) {
  return <section className="main-panel" aria-label={label}>
    <div className="main-action-bar" role="toolbar" aria-label="Screen actions">{toolbar}</div>
    <div className="main-surface">{children}</div>
  </section>
}

export function DetailsPanel({ open, expanded, style, children }: { open: boolean; expanded: boolean; style?: CSSProperties; children: ReactNode }) {
  return <aside className={`side-panel right-panel ${open ? 'is-open' : ''} ${open && expanded ? 'is-expanded' : ''}`} style={style} aria-label="Details panel">{children}</aside>
}
