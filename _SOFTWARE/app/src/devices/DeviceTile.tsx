import { Activity, Battery, Cable, CircuitBoard, Cog, Cpu, Gauge, Plug, ToggleLeft, Zap } from 'lucide-react'
import type { LucideIcon } from 'lucide-react'
import type { DeviceType } from '../domain/devices'

/*
 * A square for a device: an icon chosen from the type's own tags (the `@tags` of its `//#device` record), so a new driver gets a
 * fitting one without the app naming it.
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

const iconOf = (type: DeviceType | undefined): LucideIcon => {
  if (type?.protocols.includes('native')) return Cpu
  return BY_TAG.find(([tag]) => type?.tags.includes(tag))?.[1] ?? CircuitBoard
}

export function DeviceTile({ type, size = 'medium', title }: { type?: DeviceType; size?: 'small' | 'medium' | 'large'; title?: string }) {
  const Icon = iconOf(type)
  return (
    <span className={`device-tile is-${size}`} title={title}>
      <Icon aria-hidden="true" />
    </span>
  )
}
