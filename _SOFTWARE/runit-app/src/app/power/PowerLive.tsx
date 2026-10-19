import { ChevronDown, ChevronUp, Zap } from 'lucide-react'
import type { BoardDescriptor, Services } from '@backend'
import { usePersistedFlag } from '@ui/hooks/useStorage'
import type { DeviceInfo } from '../devices/devices'
import { commandOf, showValue, valueSpec } from './powerDisplay'
import type { PowerReading } from './powerDisplay'
import { usePowerLive } from './usePowerLive'
import './PowerLive.css'

/*
 * The Live power display: the readings the Project view's tab sets (power/PowerDisplayScreen.tsx), read every second while connected.
 * Narrow at the foot of the collapsed right panel (a reading a line), a row at the top of the open one. Readings that share a name
 * stand under it once; More shows the readings that are not in the short view.
 */

export function PowerLive({ services, devices, readings, connected, layout }: {
  services: Services
  board: BoardDescriptor
  devices: readonly DeviceInfo[]
  readings: readonly PowerReading[]
  connected: boolean
  layout: 'column' | 'row'
}) {
  const [more, setMore] = usePersistedFlag('runit.power.more', false)
  const values = usePowerLive(services, readings, connected, more)
  if (!connected || readings.length === 0) return null
  const shown = readings.map((reading, i) => ({ reading, i })).filter(({ reading }) => more || reading.main)
  // Consecutive readings of one name are one group: the name once, then their values.
  const groups: { name: string; items: { text: string; title: string; key: number }[] }[] = []
  for (const { reading, i } of shown) {
    const spec = valueSpec(commandOf(services.catalog, devices, reading.read), reading.field)
    const text = showValue(values[i], spec)
    const item = { text, title: `${reading.name}${spec.label ? ` · ${spec.label}` : ''}: ${text}`, key: i }
    const last = groups.at(-1)
    if (last && last.name === reading.name) last.items.push(item)
    else groups.push({ name: reading.name, items: [item] })
  }
  return (
    <div className={`power-live is-${layout}`} data-help="The Live power display: the readings set on the Project view's tab of that name, read every second. More shows the rest.">
      <Zap className="power-live-icon" aria-hidden="true" />
      {groups.map((group, g) => (
        <div key={g} className="power-live-group">
          <span className="power-live-name">{group.name}</span>
          {group.items.map((item) => <span key={item.key} className="power-live-value" title={item.title}>{item.text}</span>)}
        </div>
      ))}
      {readings.some((r) => !r.main) && (
        <button type="button" className="power-live-more" aria-expanded={more} aria-label={more ? 'Less power readings' : 'More power readings'} title={more ? 'Only the short view' : 'Every reading'} onClick={() => setMore(!more)}>
          {more ? <ChevronUp aria-hidden="true" /> : <ChevronDown aria-hidden="true" />}
        </button>
      )}
    </div>
  )
}
