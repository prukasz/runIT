import { Activity, Battery, Bell, Bot, Cable, Car, CircuitBoard, Cog, Cpu, Droplet, Eye, Fan, Gauge, Lightbulb, Plug, Radio, Sun, Thermometer, ToggleLeft, Volume2, Waves, Zap } from 'lucide-react'
import type { LucideIcon } from 'lucide-react'
import type { DeviceType } from '../domain/descriptors'
import type { DeviceAppearance } from '../domain/project'

/** Icons a device tile can show, by the name saved in the project. */
export const DEVICE_ICONS: Readonly<Record<string, LucideIcon>> = {
  cpu: Cpu,
  board: CircuitBoard,
  power: Zap,
  battery: Battery,
  gauge: Gauge,
  plug: Plug,
  radio: Radio,
  cable: Cable,
  toggle: ToggleLeft,
  activity: Activity,
  waves: Waves,
  light: Lightbulb,
  sun: Sun,
  fan: Fan,
  motor: Cog,
  car: Car,
  robot: Bot,
  thermometer: Thermometer,
  droplet: Droplet,
  eye: Eye,
  bell: Bell,
  speaker: Volume2,
}

/** The icon a device type shows by default, from what it provides. */
export const defaultIconOf = (type: DeviceType | undefined): string => {
  const provider = type?.provider?.label.toLowerCase() ?? ''
  if (provider.includes('monitor')) return 'gauge'
  if (provider.includes('usb')) return 'plug'
  if (provider.includes('power') || provider.includes('regulator') || provider.includes('vreg')) return 'power'
  if (provider.includes('motor') || provider.includes('bridge')) return 'motor'
  if (type?.tags.includes('pwm')) return 'activity'
  if (type?.protocols.includes('native')) return 'cpu'
  return 'board'
}

/** A square showing the device's image, its icon, or its type's default icon. */
export function DeviceTile({ appearance, type, size = 'medium', title }: { appearance?: DeviceAppearance; type?: DeviceType; size?: 'small' | 'medium' | 'large'; title?: string }) {
  const Icon = DEVICE_ICONS[appearance?.icon ?? ''] ?? DEVICE_ICONS[defaultIconOf(type)] ?? Cpu
  return (
    <span className={`device-tile is-${size}`} title={title}>
      {appearance?.image ? <img src={appearance.image} alt="" /> : <Icon aria-hidden="true" />}
    </span>
  )
}
