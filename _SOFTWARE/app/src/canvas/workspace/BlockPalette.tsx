import { useState } from 'react'
import { PaletteSearch } from '../../components/PaletteSearch'
import { PaletteSectionHeader } from '../../components/PaletteSectionHeader'
import { TreeSlab } from '../../components/TreeSlab'
import { runitVmCatalog } from '../../domain/descriptors'
import type { VmBlockType } from '../../domain/descriptors'
import type { CanvasWorkspace } from './useCanvasWorkspace'
import '../Canvas.css'

/*
 * Left panel of the canvas: the block palette, by category. Drag a block onto
 * the canvas, or click it to place it in the middle of the view (touch).
 */

/** Data type of a palette drag: the block type key. */
export const BLOCK_DRAG_TYPE = 'application/x-runit-block'

const CATEGORY_TITLES: Readonly<Record<string, string>> = { flow: 'Flow', data: 'Data', logic: 'Logic', time: 'Time', io: 'Pins', system: 'System' }
const CATEGORY_ORDER = ['flow', 'data', 'logic', 'time', 'io', 'system']

const matches = (query: string, type: VmBlockType): boolean => {
  const q = query.trim().toLowerCase()
  return !q || [type.title, type.key, type.category, type.description].some((text) => text.toLowerCase().includes(q))
}

export function BlockPalette({ workspace, tiles }: {
  workspace: CanvasWorkspace
  /** View / Manage: the types as tiles, picked to read about them (not placed); `used` counts what the program has. */
  tiles?: { readonly selectedKey: string | undefined; readonly onSelect: (key: string) => void }
}) {
  const catalog = runitVmCatalog()
  const [query, setQuery] = useState('')
  const [collapsed, setCollapsed] = useState<ReadonlySet<string>>(new Set())
  const categories = [...new Set(catalog.blocks.map((type) => type.category))].sort((a, b) => (CATEGORY_ORDER.indexOf(a) + 1 || 99) - (CATEGORY_ORDER.indexOf(b) + 1 || 99))
  const canPlace = !!workspace.active
  const used = new Map<string, number>()
  for (const canvas of workspace.canvases) for (const block of canvas.blocks) used.set(block.type, (used.get(block.type) ?? 0) + 1)

  const toggle = (category: string) => setCollapsed((current) => {
    const next = new Set(current)
    if (next.has(category)) next.delete(category)
    else next.add(category)
    return next
  })

  return (
    <div className="block-palette">
      <PaletteSearch value={query} onChange={setQuery} placeholder="Search blocks..." label="Search blocks" />
      <div className="block-palette-scroll" role="list" aria-label="Blocks">
        {categories.map((category) => {
          const types = catalog.blocks.filter((type) => type.category === category && matches(query, type))
          if (!types.length) return null
          const open = !collapsed.has(category) || !!query
          return (
            <div key={category} className="block-palette-group">
              <PaletteSectionHeader label={CATEGORY_TITLES[category] ?? category} count={types.length} open={open} onToggle={() => toggle(category)} />
              {open && tiles && (
                <div className="block-tile-grid">
                  {types.map((type) => (
                    <button
                      key={type.key}
                      type="button"
                      className={`block-tile cat-${type.category}${tiles.selectedKey === type.key ? ' selected' : ''}`}
                      aria-pressed={tiles.selectedKey === type.key}
                      title={type.description}
                      onClick={() => tiles.onSelect(type.key)}
                    >
                      <span className="block-palette-swatch" aria-hidden="true" />
                      <strong>{type.title}</strong>
                      {!!used.get(type.key) && <span className="block-tile-count" title="Blocks of this type in the program">{used.get(type.key)}</span>}
                    </button>
                  ))}
                </div>
              )}
              {open && !tiles && types.map((type) => (
                <TreeSlab
                  key={type.key}
                  role="listitem"
                  className={`block-palette-item cat-${type.category}`}
                  disabled={!canPlace}
                  draggable={canPlace}
                  title={`${type.description}\n\nDrag onto the canvas, or click to place it.`}
                  ariaLabel={type.title}
                  icon={<span className="block-palette-swatch" aria-hidden="true" />}
                  label={type.title}
                  onDragStart={(event) => {
                    workspace.paletteDrag.current = type.key
                    event.dataTransfer.setData(BLOCK_DRAG_TYPE, type.key)
                    event.dataTransfer.setData('text/plain', type.title)
                    event.dataTransfer.effectAllowed = 'copy'
                  }}
                  onDragEnd={() => { workspace.paletteDrag.current = undefined }}
                  onClick={() => workspace.placeBlock(type.key)}
                />
              ))}
            </div>
          )
        })}
      </div>
      {!canPlace && <p className="block-palette-note">Create a canvas to place blocks.</p>}
    </div>
  )
}
