import { ChevronDown, ChevronRight, Plus } from 'lucide-react'
import './PaletteSectionHeader.css'

interface AddAction {
  label: string
  onClick: () => void
}

/** A palette section heading; each palette owns its own collapsed state. */
export function PaletteSectionHeader({ label, count, open, onToggle, addAction }: {
  label: string
  count: number
  open: boolean
  onToggle: () => void
  addAction?: AddAction
}) {
  return <div className="palette-section-header">
    <button type="button" className="palette-section-toggle" aria-expanded={open} onClick={onToggle}>
      {open ? <ChevronDown aria-hidden="true" /> : <ChevronRight aria-hidden="true" />}
      <span>{label}</span>
      <em>{count}</em>
    </button>
    {addAction && <button type="button" className="palette-section-add" title={addAction.label} aria-label={addAction.label} onClick={addAction.onClick}><Plus aria-hidden="true" /></button>}
  </div>
}
