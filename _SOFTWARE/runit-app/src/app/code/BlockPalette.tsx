import { useState } from 'react'
import { ChevronDown, ChevronRight } from 'lucide-react'
import type { BlockType, Category, Variant } from '@engine'
import { bestMatch, matchesSearch, PaletteSearch, PaletteSectionHeader, TreeSlab } from '@ui/components'
import { encodePlacement } from './useBlocks'
import type { BlocksState, Placement } from './useBlocks'
import './Blocks.css'

/** Data type of a palette drag: what it places (`encodePlacement`: `periodic`, `io#set_level`). */
export const BLOCK_DRAG_TYPE = 'application/x-runit-block'

const CATEGORY_TITLES: Readonly<Record<Category, string>> = { flow: 'Flow', logic: 'Logic', data: 'Data', time: 'Time', device: 'Devices', system: 'System' }
const CATEGORY_ORDER: readonly Category[] = ['flow', 'data', 'logic', 'time', 'device', 'system']

/** How an operation of a device block acts: a reading (every pass, values to the outputs), once per enable, or a setting. */
const kindOf = (variant: Variant): { readonly kind: 'read' | 'edge' | 'change'; readonly text: string } =>
  variant.outputs.length > 0 ? { kind: 'read', text: 'A reading: asked every pass, the values go to the outputs.' }
    : variant.activation?.kind === 'enable-rising' ? { kind: 'edge', text: 'An action: sent once each time the block is enabled.' }
    : { kind: 'change', text: 'A setting: sent while the block is enabled.' }

const blockMatches = (query: string, block: BlockType): boolean =>
  !query.trim() || matchesSearch(block.alias, query) || block.tags.some((t) => matchesSearch(t, query)) || block.variants.some((v) => matchesSearch(v.alias, query))

/** The variants listed under a device block: all when the block itself matches, else those that match. */
const variantsShown = (query: string, block: BlockType): readonly Variant[] =>
  !query.trim() || matchesSearch(block.alias, query) || block.tags.some((t) => matchesSearch(t, query)) ? block.variants : block.variants.filter((v) => matchesSearch(v.alias, query))

/**
 * The block palette (left panel, Blocks): the catalog by category, a device block as a folder of its operations. A click picks the
 * block (a second click on it puts it back): the next click on the canvas places it there. A drag onto the canvas places it where
 * it is let go.
 */
export function BlockPalette({ blocks }: { blocks: BlocksState }) {
  const [query, setQuery] = useState('')
  const [collapsed, setCollapsed] = useState<ReadonlySet<string>>(new Set())
  const [open, setOpen] = useState<ReadonlySet<string>>(new Set())
  const flip = (set: ReadonlySet<string>, key: string) => {
    const next = new Set(set)
    if (!next.delete(key)) next.add(key)
    return next
  }
  const groups = blocks.catalog.categories()
  const categories = CATEGORY_ORDER.filter((c) => groups.has(c))
  const listed = (category: Category) => [...(groups.get(category) ?? [])].sort((a, b) => a.order - b.order || a.alias.localeCompare(b.alias)).filter((b) => blockMatches(query, b))

  // Every entry a search can pick, in the order shown: the best one is marked and Enter adds it.
  const entries = categories.flatMap((c) => listed(c).flatMap((b) => (b.variants.length && b.integration !== 'core'
    ? variantsShown(query, b).map((v) => ({ placement: { type: b.id, variant: v.id } as Placement, names: [v.alias, b.alias] }))
    : [{ placement: { type: b.id } as Placement, names: [b.alias, ...b.tags] }])))
  const best = query.trim() ? bestMatch(entries, (e) => e.names, query)?.placement : undefined
  const isBest = (p: Placement) => !!best && encodePlacement(best) === encodePlacement(p)
  const isArmed = (p: Placement) => !!blocks.armed && encodePlacement(blocks.armed) === encodePlacement(p)
  const pick = (p: Placement) => blocks.arm(isArmed(p) ? undefined : p)

  const dragProps = (placement: Placement, label: string) => ({
    draggable: true,
    onDragStart: (event: React.DragEvent<HTMLDivElement>) => {
      event.dataTransfer.setData(BLOCK_DRAG_TYPE, encodePlacement(placement))
      event.dataTransfer.setData('text/plain', label)
      event.dataTransfer.effectAllowed = 'copy'
      blocks.paletteDrag.current = placement
    },
    onDragEnd: () => { blocks.paletteDrag.current = undefined },
  })

  return (
    <div className="block-palette">
      <PaletteSearch value={query} onChange={setQuery} placeholder="Search blocks" label="Search blocks" onEnter={() => best && blocks.arm(best)} />
      <div className="block-palette-scroll" role="list" aria-label="Blocks">
        {categories.map((category) => {
          const found = listed(category)
          if (!found.length) return null
          const shown = !collapsed.has(category) || !!query.trim()
          return (
            <div key={category} className="block-palette-group">
              <PaletteSectionHeader label={CATEGORY_TITLES[category]} count={found.length} open={shown} onToggle={() => setCollapsed((c) => flip(c, category))} />
              {shown && found.map((block) => {
                if (!(block.variants.length && block.integration !== 'core')) {
                  const placement = { type: block.id }
                  return (
                    <TreeSlab key={block.id} role="listitem" className={`block-palette-item cat-${block.category}`} isMatch={isBest(placement)} selected={isArmed(placement)} title={block.description} ariaLabel={block.alias}
                      icon={<span className="block-palette-swatch" aria-hidden="true" />} label={block.alias} {...dragProps(placement, block.alias)} onClick={() => pick(placement)} />
                  )
                }
                const folderOpen = open.has(block.id) || !!query.trim()
                return (
                  <div key={block.id} className={`block-palette-folder cat-${block.category}`}>
                    <TreeSlab role="listitem" className={`block-palette-item cat-${block.category}`} isFolder title={block.description} ariaLabel={`${block.alias}: ${block.variants.length} operations`}
                      icon={<span className="block-palette-swatch" aria-hidden="true" />} label={block.alias} onClick={() => setOpen((o) => flip(o, block.id))}
                      disclosure={folderOpen ? <ChevronDown aria-hidden="true" /> : <ChevronRight aria-hidden="true" />} />
                    {folderOpen && (
                      <div className="block-palette-ops">
                        {variantsShown(query, block).map((variant) => {
                          const placement = { type: block.id, variant: variant.id }
                          const kind = kindOf(variant)
                          return (
                            <TreeSlab key={variant.id} role="listitem" className={`block-palette-item block-palette-op cat-${block.category}`} isMatch={isBest(placement)} selected={isArmed(placement)}
                              title={`${variant.description ?? variant.alias}\n\n${kind.text}`} ariaLabel={`${block.alias}: ${variant.alias}`}
                              icon={<span className={`block-palette-op-kind is-${kind.kind}`} aria-hidden="true" />} label={variant.alias}
                              {...dragProps(placement, `${block.alias}: ${variant.alias}`)} onClick={() => pick(placement)} />
                          )
                        })}
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
