import { useState } from 'react'
import { ChevronDown, ChevronRight, Search, X } from 'lucide-react'
import { runitVmCatalog } from '../domain/descriptors'
import type { VmBlockType } from '../domain/descriptors'
import type { CanvasWorkspace } from './useCanvasWorkspace'
import './Canvas.css'

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

export function BlockPalette({ workspace }: { workspace: CanvasWorkspace }) {
  const catalog = runitVmCatalog()
  const [query, setQuery] = useState('')
  const [collapsed, setCollapsed] = useState<ReadonlySet<string>>(new Set())
  const categories = [...new Set(catalog.blocks.map((type) => type.category))].sort((a, b) => (CATEGORY_ORDER.indexOf(a) + 1 || 99) - (CATEGORY_ORDER.indexOf(b) + 1 || 99))
  const canPlace = !!workspace.active

  const toggle = (category: string) => setCollapsed((current) => {
    const next = new Set(current)
    if (next.has(category)) next.delete(category)
    else next.add(category)
    return next
  })

  return (
    <div className="block-palette">
      <div className="object-tree-search-bar">
        <Search className="search-icon" aria-hidden="true" />
        <input type="text" placeholder="Search blocks..." aria-label="Search blocks" value={query} onChange={(event) => setQuery(event.target.value)} onKeyDown={(event) => { if (event.key === 'Escape') setQuery('') }} />
        {query && <button type="button" className="search-clear-btn" title="Clear search" aria-label="Clear search" onClick={() => setQuery('')}><X aria-hidden="true" /></button>}
      </div>
      <div className="block-palette-scroll" role="list" aria-label="Blocks">
        {categories.map((category) => {
          const types = catalog.blocks.filter((type) => type.category === category && matches(query, type))
          if (!types.length) return null
          const open = !collapsed.has(category) || !!query
          return (
            <div key={category} className="block-palette-group">
              <button type="button" className="block-palette-heading" aria-expanded={open} onClick={() => toggle(category)}>
                {open ? <ChevronDown aria-hidden="true" /> : <ChevronRight aria-hidden="true" />}
                <span>{CATEGORY_TITLES[category] ?? category}</span>
                <em>{types.length}</em>
              </button>
              {open && types.map((type) => (
                <div
                  key={type.key}
                  role="listitem"
                  tabIndex={0}
                  className={`block-palette-item cat-${type.category} ${canPlace ? '' : 'is-disabled'}`}
                  draggable={canPlace}
                  title={`${type.description}\n\nDrag onto the canvas, or click to place it.`}
                  onDragStart={(event) => {
                    workspace.paletteDrag.current = type.key
                    event.dataTransfer.setData(BLOCK_DRAG_TYPE, type.key)
                    event.dataTransfer.setData('text/plain', type.title)
                    event.dataTransfer.effectAllowed = 'copy'
                  }}
                  onDragEnd={() => { workspace.paletteDrag.current = undefined }}
                  onClick={() => canPlace && workspace.placeBlock(type.key)}
                  onKeyDown={(event) => { if ((event.key === 'Enter' || event.key === ' ') && canPlace) { event.preventDefault(); workspace.placeBlock(type.key) } }}
                >
                  <span className="block-palette-swatch" aria-hidden="true" />
                  <span className="block-palette-title">{type.title}</span>
                  <span className="block-palette-key">{type.key}</span>
                </div>
              ))}
            </div>
          )
        })}
      </div>
      {!canPlace && <p className="block-palette-note">Create a canvas to place blocks.</p>}
    </div>
  )
}
