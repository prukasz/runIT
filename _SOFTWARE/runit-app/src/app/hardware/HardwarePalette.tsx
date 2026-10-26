import { useState } from 'react'
import { Badge, Button, PaletteSearch, PaletteSectionHeader, matchesSearch } from '@ui/components'
import { CATEGORY_LABELS, svgUrl } from './library'
import type { LibraryPart, PartCategory } from './library'
import { rangeText } from './power'
import type { HardwareState } from './useHardware'
import { PART_DRAG_TYPE } from './useHardware'

/*
 * The left panel of the Devices canvas: the hardware library by category, each part a tile with the miniature of its drawing.
 * A click picks a part (the next click on the canvas places it; a second click or Esc puts it back), a drag drops it. Boards and
 * bare chips are not placed here: the board is the project's, chips are for the creator. The creator opens from the button.
 */

const PLACEABLE: readonly PartCategory[] = ['actuator', 'module', 'sensor', 'passive', 'power']

/** What a tile says under the name: the voltage it wants and what it draws, or what it carries. */
const meta = (part: LibraryPart): string | undefined => {
  const sink = part.nets?.find((n) => n.role === 'sink' && n.voltage_mV)
  if (!sink) return part.device ? part.device.type : undefined
  const draw = sink.current_mA?.typical
  return `${rangeText(sink.voltage_mV)}${draw ? ` · ${draw} mA` : ''}`
}

export function HardwarePalette({ state }: { state: HardwareState }) {
  const [search, setSearch] = useState('')
  const [closed, setClosed] = useState<ReadonlySet<string>>(new Set())
  const parts = [...state.library.values()].filter((p) => PLACEABLE.includes(p.category))
  const shown = parts.filter((p) => !search || [p.name, p.description ?? '', ...(p.tags ?? []), p.device?.type ?? ''].some((text) => matchesSearch(text, search)))
  const first = shown[0]
  return (
    <div className="hw-palette">
      <PaletteSearch value={search} onChange={setSearch} placeholder="Search parts" label="Search the parts" onEnter={() => first && state.arm(first.id)} />
      <Button block variant="dashed" onClick={() => state.openCreator()}>New part…</Button>
      {PLACEABLE.map((category) => {
        const list = shown.filter((p) => p.category === category)
        if (list.length === 0) return null
        const open = !closed.has(category) || search !== ''
        return (
          <section key={category}>
            <PaletteSectionHeader label={CATEGORY_LABELS[category]} count={list.length} open={open}
              onToggle={() => setClosed((c) => { const next = new Set(c); if (next.has(category)) next.delete(category); else next.add(category); return next })} />
            {open && (
              <div className="hw-palette-grid">
                {list.map((part) => (
                  <div key={part.id} className={`hw-tile ${state.armed === part.id ? 'is-armed' : ''}`.trim()} role="button" tabIndex={0} title={part.description ?? part.name} draggable
                    onDragStart={(e) => { e.dataTransfer.setData(PART_DRAG_TYPE, part.id); e.dataTransfer.effectAllowed = 'copy' }}
                    onClick={() => state.arm(state.armed === part.id ? undefined : part.id)}
                    onKeyDown={(e) => { if (e.key === 'Enter') state.arm(part.id) }}>
                    <img src={svgUrl(part.svg)} alt="" />
                    <span className="hw-tile-name">{part.name}</span>
                    {meta(part) && <span className="hw-tile-meta">{meta(part)}</span>}
                    {part.origin === 'project' && <Badge tone="info">this project</Badge>}
                  </div>
                ))}
              </div>
            )}
          </section>
        )
      })}
    </div>
  )
}
