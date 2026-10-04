import { useMemo, useState } from 'react'
import { ArrowLeftRight } from 'lucide-react'
import { Badge } from '../components/Badge'
import { PaletteSearch } from '../components/PaletteSearch'
import { PaletteSectionHeader } from '../components/PaletteSectionHeader'
import { TreeSlab } from '../components/TreeSlab'
import { CONNECTOR_TYPES } from '../domain/board'
import type { ConnectorType } from '../domain/board'
import { CONNECTOR_STYLE } from '../domain/board/connectors'
import { scopeSvg } from './board/boardArt'
import type { BoardEditorApi } from './useBoardEditor'
import './Devices.css'

/*
 * The board creator's left panel: the premade connectors, grouped. A click adds one at the middle of the board; the tile
 * is a small drawing of the part from the same art the board uses.
 */

const PREVIEW_SCOPE = 'connector-preview'

/** A connector drawn small, with the board art's style scoped to the preview. */
function previewMarkup(type: ConnectorType): string {
  const margin = 0.8
  const raw = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="${-type.width / 2 - margin} ${-type.height / 2 - margin} ${type.width + 2 * margin} ${type.height + 2 * margin}"><style>\n${CONNECTOR_STYLE}\n</style>${type.draw((_, shape) => shape)}</svg>`
  return scopeSvg(raw, PREVIEW_SCOPE).markup
}

const GROUPS: readonly ConnectorType['group'][] = ['Screw terminals', 'Pin headers', 'Power']

export function BoardPalette({ editor: e, onClose }: { editor: BoardEditorApi; onClose: () => void }) {
  const [query, setQuery] = useState('')
  const [collapsed, setCollapsed] = useState<ReadonlySet<string>>(new Set())
  const previews = useMemo(() => new Map(CONNECTOR_TYPES.map((type) => [type.id, previewMarkup(type)])), [])
  const toggle = (group: string) => setCollapsed((current) => {
    const next = new Set(current)
    if (next.has(group)) next.delete(group)
    else next.add(group)
    return next
  })
  const q = query.trim().toLowerCase()
  const shown = (type: ConnectorType) => !q || type.label.toLowerCase().includes(q) || type.group.toLowerCase().includes(q) || type.id.includes(q)

  return (
    <div className="code-palette devices-palette board-palette">
      <div className="devices-palette-body">
        <PaletteSearch value={query} onChange={setQuery} placeholder="Search connectors..." label="Search connectors" />
        <div className="object-tree-scroll" role="navigation" aria-label="Connectors">
          {!e.draft && <p className="devices-muted board-palette-hint">Create a board first (toolbar), then add connectors here.</p>}
          {GROUPS.map((group) => {
            const types = CONNECTOR_TYPES.filter((type) => type.group === group && shown(type))
            if (!types.length) return null
            return (
              <div key={group}>
                <PaletteSectionHeader label={group} count={types.length} open={!collapsed.has(group)} onToggle={() => toggle(group)} />
                {!collapsed.has(group) && (
                  <div className="devices-folder-items">
                    {types.map((type) => (
                      <TreeSlab
                        key={type.id}
                        icon={<span className={`${PREVIEW_SCOPE} board-palette-preview`} dangerouslySetInnerHTML={{ __html: previews.get(type.id) ?? '' }} />}
                        label={type.label}
                        title={`Add a ${type.label} to the board`}
                        badges={<Badge tone="count" size="compact">{type.pins.length} {type.pins.length === 1 ? 'pin' : 'pins'}</Badge>}
                        disabled={!e.draft}
                        onClick={() => e.addConnector(type.id)}
                      />
                    ))}
                  </div>
                )}
              </div>
            )
          })}
        </div>
      </div>
      <div className="code-mode-footer">
        <button className="code-mode-toggle" onClick={onClose}><ArrowLeftRight aria-hidden="true" /><span>Back to Board canvas</span></button>
      </div>
    </div>
  )
}
