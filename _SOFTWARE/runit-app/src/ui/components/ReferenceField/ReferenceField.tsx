import { CircuitBoard, TriangleAlert } from 'lucide-react'
import type { LucideIcon } from 'lucide-react'
import { DeviceTile } from '../DeviceTile/DeviceTile'
import './ReferenceField.css'

/**
 * A pill that names another thing of the project (a device and its pin, a variable by its path, a block pin) and jumps to it.
 * It names, never shows an id. Without `onJump` it is disabled (the target is not there to open); `empty` is a reference not set
 * yet (dashed, a click chooses); `broken` is one whose target does not exist (red, the alert icon).
 */
export function RefPill({ name, detail, icon = CircuitBoard, compact = false, empty = false, broken = false, arrived = false, refKey, title, onJump }: {
  name: string
  /** What inside the target: a pin (`P26`), a channel, a block pin (`Run when`). */
  detail?: string
  /** The tile's icon: a device's own, a link for a variable, a box for a block. */
  icon?: LucideIcon
  /** The smaller pill inside a row or an error entry. */
  compact?: boolean
  empty?: boolean
  broken?: boolean
  /** Flashes once: the view just jumped here. */
  arrived?: boolean
  /** The key of the target (`refKey`, BACKEND.md): what an error marks. */
  refKey?: string
  title?: string
  onJump?: () => void
}) {
  const classes = ['ref-pill', compact && 'is-compact', empty && 'is-empty', broken && 'is-broken', arrived && 'is-arrived'].filter(Boolean).join(' ')
  return (
    <button type="button" className={classes} disabled={!onJump} data-key={refKey} title={title ?? (empty ? 'Choose' : onJump ? `Open ${name}` : 'Missing')}
      onClick={(event) => { event.stopPropagation(); onJump?.() }}>
      <DeviceTile size="small" icon={broken ? TriangleAlert : icon} />
      <span className="ref-pill-name">{name}</span>
      {detail && <span className="ref-pill-detail">{detail}</span>}
    </button>
  )
}
