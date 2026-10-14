import { useState } from 'react'
import { ChevronDown, ChevronRight } from 'lucide-react'
import { bestMatch, matchesSearch, PaletteSearch } from '../components/PaletteSearch'
import { PaletteSectionHeader } from '../components/PaletteSectionHeader'
import { TreeSlab } from '../components/TreeSlab'
import { BLOCKS } from '../backend/vm/blocks'
import type { BlockDescriptor } from '../backend/vm/blocks'
import { encodePlacement, typeKeyOf } from './blockModel'
import './Blocks.css'

/*
 * The block palette, by category. A block is picked to read about it; dragging onto a canvas follows with the canvas.
 */

/** Data type of a palette drag: the block's type key (`EXPR`). */
export const BLOCK_DRAG_TYPE = 'application/x-runit-block'

const CATEGORY_TITLES: Readonly<Record<string, string>> = { flow: 'Flow', data: 'Data', logic: 'Logic', time: 'Time', device: 'Devices', system: 'System' }
const CATEGORY_ORDER = ['flow', 'data', 'logic', 'time', 'device', 'system']

const blocks: readonly BlockDescriptor[] = [...BLOCKS.values()].sort((a, b) => a.alias.localeCompare(b.alias))

const OP_KIND = { change: 'A setting: sent when it changes.', edge: 'An action: sent once each time the enable turns on.', read: 'A reading: asked every pass, the values go to the outputs.' } as const

/** A block is listed when its name matches (the search of the variables palette) or, for a device block, one of its operations. */
const blockMatches = (query: string, block: BlockDescriptor): boolean =>
  !query.trim() || matchesSearch(block.alias, query) || matchesSearch(block.name, query) || !!block.ops?.some((op) => matchesSearch(op.alias, query))

/** The operations of a device block listed under it: all when the block itself matches, else those that match. */
const opsShown = (query: string, block: BlockDescriptor) =>
  !query.trim() || matchesSearch(block.alias, query) || matchesSearch(block.name, query) ? block.ops ?? [] : (block.ops ?? []).filter((op) => matchesSearch(op.alias, query))

export function BlockPalette({ selected, onSelect, onDragType }: {
  /** The picked entry, as `encodePlacement` words it (`EXPR`, `SERVO#65282`). */
  selected: string | undefined
  /** An entry picked: it waits for the Canvas button, which places it. */
  onSelect: (placement: string) => void
  /** What is being dragged (undefined when the drag ends): the canvas shows where it would land. */
  onDragType?: (placement: string | undefined) => void
}) {
  const [query, setQuery] = useState('')
  const [collapsed, setCollapsed] = useState<ReadonlySet<string>>(new Set())
  // The device blocks open to their operations.
  const [openBlocks, setOpenBlocks] = useState<ReadonlySet<string>>(new Set())
  const toggleBlock = (name: string) => setOpenBlocks((current) => {
    const next = new Set(current)
    if (!next.delete(name)) next.add(name)
    return next
  })
  const categories = [...new Set(blocks.map((block) => block.category))].sort((a, b) => (CATEGORY_ORDER.indexOf(a) + 1 || 99) - (CATEGORY_ORDER.indexOf(b) + 1 || 99))

  const toggle = (category: string) => setCollapsed((current) => {
    const next = new Set(current)
    if (next.has(category)) next.delete(category)
    else next.add(category)
    return next
  })

  // Every entry a search can pick, in display order: the best one is highlighted and Enter picks it.
  const entries = categories.flatMap((category) => blocks.filter((block) => block.category === category && blockMatches(query, block)).flatMap((block) =>
    block.ops ? opsShown(query, block).map((op) => ({ placement: encodePlacement(typeKeyOf(block), op.value), names: [op.alias, block.alias] })) : [{ placement: encodePlacement(typeKeyOf(block)), names: [block.alias, block.name] }]))
  const best = query.trim() ? bestMatch(entries, (entry) => entry.names, query)?.placement : undefined

  return (
    <div className="block-palette">
      <PaletteSearch value={query} onChange={setQuery} placeholder="Search blocks..." label="Search blocks" onEnter={() => best && onSelect(best)} />
      <div className="block-palette-scroll" role="list" aria-label="Blocks">
        {categories.map((category) => {
          const found = blocks.filter((block) => block.category === category && blockMatches(query, block))
          if (!found.length) return null
          const open = !collapsed.has(category) || !!query
          return (
            <div key={category} className="block-palette-group">
              <PaletteSectionHeader label={CATEGORY_TITLES[category] ?? category} count={found.length} open={open} onToggle={() => toggle(category)} />
              {open && found.map((block) => {
                const key = typeKeyOf(block)
                if (!block.ops) {
                  return (
                    <TreeSlab
                      key={block.name}
                      role="listitem"
                      className={`block-palette-item cat-${block.category}`}
                      selected={selected === key}
                      isMatch={best === encodePlacement(key)}
                      draggable
                      title={block.description}
                      ariaLabel={block.alias}
                      icon={<span className="block-palette-swatch" aria-hidden="true" />}
                      label={block.alias}
                      onDragStart={(event) => {
                        event.dataTransfer.setData(BLOCK_DRAG_TYPE, encodePlacement(key))
                        onDragType?.(encodePlacement(key))
                        event.dataTransfer.setData('text/plain', block.alias)
                        event.dataTransfer.effectAllowed = 'copy'
                      }}
                      onDragEnd={() => onDragType?.(undefined)}
                      onClick={() => onSelect(encodePlacement(key))}
                    />
                  )
                }
                // A device block: a folder, one entry per operation; the operation is set when the block is placed.
                const folderOpen = openBlocks.has(block.name) || !!query
                return (
                  <div key={block.name} className={`block-palette-folder cat-${block.category}`}>
                    <TreeSlab
                      role="listitem"
                      className={`block-palette-item cat-${block.category}`}
                      isFolder
                      selected={selected?.split('#')[0] === key}
                      title={block.description}
                      ariaLabel={`${block.alias}: ${block.ops.length} operations`}
                      icon={<span className="block-palette-swatch" aria-hidden="true" />}
                      label={block.alias}
                      onClick={() => toggleBlock(block.name)}
                      disclosure={folderOpen ? <ChevronDown aria-hidden="true" /> : <ChevronRight aria-hidden="true" />}
                    />
                    {folderOpen && (
                      <div className="block-palette-ops">
                        {opsShown(query, block).map((op) => (
                          <TreeSlab
                            key={op.symbol}
                            role="listitem"
                            className={`block-palette-item block-palette-op cat-${block.category}`}
                            draggable
                            selected={selected === encodePlacement(key, op.value)}
                            isMatch={best === encodePlacement(key, op.value)}
                            title={`${op.description}\n\n${OP_KIND[op.kind]}`}
                            ariaLabel={`${block.alias}: ${op.alias}`}
                            icon={<span className={`block-palette-op-kind is-${op.kind}`} aria-hidden="true" />}
                            label={op.alias}
                            onDragStart={(event) => {
                              event.dataTransfer.setData(BLOCK_DRAG_TYPE, encodePlacement(key, op.value))
                              onDragType?.(encodePlacement(key, op.value))
                              event.dataTransfer.setData('text/plain', `${block.alias}: ${op.alias}`)
                              event.dataTransfer.effectAllowed = 'copy'
                            }}
                            onDragEnd={() => onDragType?.(undefined)}
                            onClick={() => onSelect(encodePlacement(key, op.value))}
                          />
                        ))}
                      </div>
                    )}
                  </div>
                )
              })}
            </div>
          )
        })}
      </div>
    </div>
  )
}
