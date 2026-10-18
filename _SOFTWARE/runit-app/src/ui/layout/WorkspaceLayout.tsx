import { useState } from 'react'
import type { CSSProperties, KeyboardEvent, PointerEvent, ReactNode } from 'react'
import { FRAME_GAP, KEY_STEP, viewWidth } from './panelWidth'

/** The app's stable three-column frame. Screens supply their own content. */
export function WorkspaceShell({ className = '', children }: { className?: string; children: ReactNode }) {
  return <main className={`shell ${className}`.trim()}>{children}</main>
}

export function ExplorerPanel({ open, width, children }: { open: boolean; width: number; children: ReactNode }) {
  return <aside className={`side-panel left-panel ${open ? 'is-open' : ''}`} style={open ? { width } : undefined} aria-label="Explorer">{children}</aside>
}

/** The centre: a row of actions (`toolbar`) over the screen itself. */
export function MainScreen({ label, toolbar, children }: { label: string; toolbar: ReactNode; children: ReactNode }) {
  return (
    <section className="main-panel" aria-label={label}>
      <div className="main-action-bar" role="toolbar" aria-label="Screen actions">{toolbar}</div>
      <div className="main-surface">{children}</div>
    </section>
  )
}

export function DetailsPanel({ open, style, children }: { open: boolean; style?: CSSProperties; children: ReactNode }) {
  return <aside className={`side-panel right-panel ${open ? 'is-open' : ''}`} style={style} aria-label="Details panel">{children}</aside>
}

/** The grip in the gap beside a side panel (a sibling of the panels, so the cards' rounded clip does not cut it): dragged with the pointer, moved with the arrow keys. */
export function ResizeHandle({ side, label, width, onResize, onStart, onEnd }: {
  side: 'left' | 'right'
  label: string
  width: number
  /** The pointer's x in the window (or, from the keys, the x the edge would have). */
  onResize: (clientX: number) => void
  onStart: () => void
  onEnd: () => void
}) {
  const [active, setActive] = useState(false)
  const end = () => { setActive(false); onEnd() }
  const key = (event: KeyboardEvent) => {
    if (event.key !== 'ArrowLeft' && event.key !== 'ArrowRight') return
    event.preventDefault()
    const grow = (event.key === 'ArrowRight') === (side === 'left') ? KEY_STEP : -KEY_STEP
    onResize(side === 'left' ? FRAME_GAP + width + grow : viewWidth() - FRAME_GAP - (width + grow))
  }
  return (
    <div
      className={`panel-resize-handle ${side}-resize-handle ${active ? 'active' : ''}`}
      role="separator"
      aria-label={label}
      aria-orientation="vertical"
      tabIndex={0}
      onPointerDown={(event: PointerEvent) => { event.currentTarget.setPointerCapture(event.pointerId); setActive(true); onStart() }}
      onPointerMove={(event) => { if (active) onResize(event.clientX) }}
      onPointerUp={end}
      onPointerCancel={end}
      onKeyDown={key}
    />
  )
}
