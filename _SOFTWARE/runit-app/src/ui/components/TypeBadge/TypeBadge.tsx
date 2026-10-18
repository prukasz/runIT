import { Folder, Link2, ToggleLeft, ToggleRight } from 'lucide-react'
import { Badge } from '../Badge/Badge'
import type { BadgeTone } from '../Badge/Badge'
import './TypeBadge.css'

/*
 * What a variable or a pin holds, at a glance. The engine's element types (`VALUE_TYPES`: bool, u8, u32, i32, f32, str) are the
 * names; a folder and a reference have a lead of their own. The UI kit (design/ui-kit.html) shows every case.
 */

/** The badge colour of an element type: bool and str have their own, every number type shares one. */
export const typeTone = (type: string): BadgeTone => (type === 'bool' ? 'boolean' : type === 'str' ? 'text' : 'number')

/** The element type as a monospace badge (`f32`), with the element count before it when there is one. */
export function TypeBadge({ type, count, title }: { type: string; count?: number; title?: string }) {
  return (
    <span className="type-badge-group" title={title ?? type}>
      {count !== undefined && <Badge tone="count">{count}</Badge>}
      <Badge tone={typeTone(type)} className="is-mono">{type}</Badge>
    </span>
  )
}

export type LeadKind = 'number' | 'bool' | 'text' | 'folder' | 'reference'

/** The lead kind of an element type. */
export const leadOf = (type: string): LeadKind => (type === 'bool' ? 'bool' : type === 'str' ? 'text' : 'number')

/**
 * The square at the start of a variable's row: `123` a number, `abc` text, a toggle a boolean (green / red with `value`, a click
 * on it flips it when `onToggle` is given), a folder, a link a reference.
 */
export function TypeLead({ kind, value, title, onToggle }: { kind: LeadKind; value?: boolean; title?: string; onToggle?: () => void }) {
  if (kind === 'number') return <span className="object-type-icon number" title={title}><span className="object-number-glyph">123</span></span>
  if (kind === 'text') return <span className="object-type-icon text" title={title}><span className="object-text-glyph">abc</span></span>
  if (kind === 'folder') return <span className="object-type-icon folder" title={title}><Folder aria-hidden="true" /></span>
  if (kind === 'reference') return <span className="object-type-icon reference" title={title}><Link2 aria-hidden="true" /></span>
  const state = value === undefined ? '' : value ? ' is-true' : ' is-false'
  const Icon = value ? ToggleRight : ToggleLeft
  return (
    <span className={`object-type-icon bool${state}`} title={title ?? (value === undefined ? undefined : String(value))}
      role={onToggle ? 'button' : undefined} tabIndex={onToggle ? 0 : undefined} style={onToggle ? { cursor: 'pointer' } : undefined}
      onClick={onToggle && ((event) => { event.stopPropagation(); onToggle() })}
      onKeyDown={onToggle && ((event) => { if (event.key === 'Enter' || event.key === ' ') { event.preventDefault(); event.stopPropagation(); onToggle() } })}>
      <Icon aria-hidden="true" />
    </span>
  )
}
