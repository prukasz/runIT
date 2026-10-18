import { Activity, Battery, Cable, CircuitBoard, Cog, Gauge, Plug, ToggleLeft, Zap } from 'lucide-react'
import type { LucideIcon } from 'lucide-react'
import './DeviceTile.css'

/*
 * A square for a device (or another thing a row or a pill stands for: a block, a variable). A device's icon comes from its
 * descriptor's tags, so a new driver gets a fitting one without the app naming it.
 */

const BY_TAG: readonly (readonly [string, LucideIcon])[] = [
  ['monitor', Gauge],
  ['usb-c', Plug],
  ['regulator', Zap],
  ['supply', Battery],
  ['power', Zap],
  ['motor', Cog],
  ['servo', Cog],
  ['pwm', Activity],
  ['gpio', ToggleLeft],
  ['adc', Cable],
  ['dac', Cable],
]

/** The icon of a device type by its tags (`CatalogEntry.tags`). */
export const deviceIcon = (tags: readonly string[] = []): LucideIcon => BY_TAG.find(([tag]) => tags.includes(tag))?.[1] ?? CircuitBoard

export function DeviceTile({ tags, icon, size = 'medium', title }: {
  /** A device type's tags: they pick the icon. */
  tags?: readonly string[]
  /** An icon of its own (a block, a variable, a broken reference): wins over `tags`. */
  icon?: LucideIcon
  size?: 'small' | 'medium'
  title?: string
}) {
  const Icon = icon ?? deviceIcon(tags)
  return (
    <span className={`device-tile is-${size}`} title={title}>
      <Icon aria-hidden="true" />
    </span>
  )
}
