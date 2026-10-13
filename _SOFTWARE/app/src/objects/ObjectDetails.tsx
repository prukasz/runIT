import { ArrowDown, ArrowUp, Boxes, ChevronRight, Folder, Grid2X2, Link2 } from 'lucide-react'
import { Badge } from '../components/Badge'
import { ChoiceField } from '../components/ChoiceField'
import { NumberField } from '../components/FormField'
import { PanelHeader } from '../components/PanelHeader'
import { Switch } from '../components/Switch/Switch'
import { maxElementsOf, valueTypes } from '../domain/objects/catalog'
import { findObject, moveObject, updateObject, walkObjects } from '../domain/project'
import type { FolderNode, ValueNode } from '../domain/project'
import { ObjectDesignator } from './ObjectTreePalette'
import { DEFAULT_STR_LENGTH } from './useObjectTree'
import type { ObjectWorkspace } from './useObjectTree'
import './ObjectDetails.css'

const typeChoices = valueTypes.map((type) => ({ value: type.key, label: type.key === 'B' ? 'T/F (Boolean)' : `${type.key} · ${type.alias}` }))

/** Properties of the selected object: its shape, location, description and flags. */
export function ObjectDetails({ workspace: w }: { workspace: ObjectWorkspace }) {
  const selected = w.selected?.node
  if (!selected) return <div className="object-details"><p>Select an item to see its properties.</p></div>

  const target = selected.kind === 'reference' ? findObject(w.project, selected.targetId)?.node : selected
  const related = new Map<string, string>()
  const availableFolders: { id: string; name: string }[] = []
  if (w.selected?.parent) related.set(w.selected.parent.id, w.selected.parent.name)
  const targetParent = target ? findObject(w.project, target.id)?.parent : undefined
  if (targetParent) related.set(targetParent.id, targetParent.name)
  walkObjects(w.project.objects, (node, ancestors) => {
    if (node.kind === 'folder' && node.id !== selected.id && !ancestors.some((folder) => folder.id === selected.id)) availableFolders.push({ id: node.id, name: `${'  '.repeat(ancestors.length)}${node.name}` })
    if (node.kind === 'reference' && node.targetId === target?.id) {
      const parent = ancestors.at(-1)
      if (parent) related.set(parent.id, parent.name)
    }
  })

  // A folder of value rows is a 2D array, a folder of such folders a 3D array.
  const is2DArray = selected.kind === 'folder' && selected.children.length > 0 && selected.children.every((c) => c.kind === 'value')
  const rows2D = is2DArray ? selected.children.length : 0
  const rowChildren2D = is2DArray ? (selected.children as ValueNode[]) : []
  const lengths2D = rowChildren2D.map((c) => c.length)
  const isUniformCols2D = lengths2D.length > 0 && lengths2D.every((l) => l === lengths2D[0])
  const cols2D = isUniformCols2D ? lengths2D[0] : 0
  const types2D = rowChildren2D.map((c) => c.type)
  const type2D = types2D.length > 0 && types2D.every((t) => t === types2D[0]) ? types2D[0] : 'mixed'

  const is3DArray = selected.kind === 'folder' && selected.children.length > 0 && selected.children.every((c) => c.kind === 'folder' && c.children.length > 0 && c.children.every((cc) => cc.kind === 'value'))
  const depth3D = is3DArray ? selected.children.length : 0
  const rows3D = is3DArray ? (selected.children[0] as FolderNode).children.length : 0
  const cols3D = is3DArray ? ((selected.children[0] as FolderNode).children[0] as ValueNode).length : 0
  const type3D = is3DArray ? ((selected.children[0] as FolderNode).children[0] as ValueNode).type : 'F'

  const displayName = target?.name || selected.name || (w.selected?.parent ? `${w.selected.parent.name || 'folder'}[${w.selected.index}]` : `[${w.selected?.index ?? 0}]`)
  const patch = (fields: Parameters<typeof updateObject>[2], key?: string) => w.edit((current) => updateObject(current, selected.id, fields), key ? { key } : undefined)

  return <div className="object-details">
    <PanelHeader icon={<ObjectDesignator node={selected} />} title={displayName} />

    {is2DArray && (
      <div className="object-details-section">
        <div className="object-details-section-title">
          <h3>2D array</h3>
          {isUniformCols2D
            ? <Badge tone="accent" size="detail">{rows2D} × {cols2D} = {rows2D * cols2D} items</Badge>
            : <Badge tone="warning" size="detail">{rows2D} rows · mixed items</Badge>}
        </div>
        <div className="object-details-grid">
          <div className="suffix-field"><NumberField aria-label="Rows" integer min={1} max={256} value={rows2D} onCommit={(rows) => w.resizeArray2D(selected.id, rows, cols2D, type2D)} /><span>rows</span></div>
          <div className="suffix-field"><NumberField aria-label="Columns" integer min={1} max={1024} value={cols2D || 1} onCommit={(cols) => w.resizeArray2D(selected.id, rows2D, cols, type2D === 'mixed' ? undefined : type2D)} /><span>columns</span></div>
        </div>
        <ChoiceField<string> aria-label="Element type" value={type2D} onChange={(next) => next !== undefined && w.resizeArray2D(selected.id, rows2D, cols2D, next)} choices={typeChoices} />
      </div>
    )}

    {is3DArray && (
      <div className="object-details-section">
        <div className="object-details-section-title">
          <h3>3D array</h3>
          <Badge tone="accent" size="detail">{depth3D} × {rows3D} × {cols3D} = {depth3D * rows3D * cols3D} items</Badge>
        </div>
        <div className="object-details-grid-3">
          <div className="suffix-field"><NumberField aria-label="Depth" integer min={1} max={64} value={depth3D} onCommit={(depth) => w.resizeArray3D(selected.id, depth, rows3D, cols3D, type3D)} /><span>planes</span></div>
          <div className="suffix-field"><NumberField aria-label="Rows" integer min={1} max={128} value={rows3D} onCommit={(rows) => w.resizeArray3D(selected.id, depth3D, rows, cols3D, type3D)} /><span>rows</span></div>
          <div className="suffix-field"><NumberField aria-label="Columns" integer min={1} max={512} value={cols3D} onCommit={(cols) => w.resizeArray3D(selected.id, depth3D, rows3D, cols, type3D)} /><span>columns</span></div>
        </div>
        <ChoiceField<string> aria-label="Element type" value={type3D} onChange={(next) => next !== undefined && w.resizeArray3D(selected.id, depth3D, rows3D, cols3D, next)} choices={typeChoices} />
      </div>
    )}

    {selected.kind === 'folder' && !is2DArray && !is3DArray && (
      <div className="object-details-section">
        <h3>Folder actions</h3>
        <div className="object-details-grid">
          <button onClick={() => w.beginLink()}><Link2 />Link variable</button>
          <button onClick={() => w.addArray2D(selected.id)}><Grid2X2 />2D array</button>
          <button onClick={() => w.addArray3D(selected.id)}><Boxes />3D array</button>
        </div>
      </div>
    )}

    {selected.kind === 'value' && (
      <div className="object-details-section">
        <div className="object-details-section-title">
          <h3>Variable</h3>
          {selected.length > 1 && <Badge tone="accent" size="detail">{selected.length} items</Badge>}
        </div>
        <div className="object-details-grid">
          <ChoiceField<string> aria-label="Type" value={selected.typeMode === 'auto' ? 'auto' : selected.type}
              onChange={(type) => {
                if (type === undefined) return
                const resolvedType = type === 'auto' ? (typeof selected.value === 'string' ? 'STR' : selected.type === 'B' ? 'B' : 'F') : type
                const length = resolvedType === 'STR' ? Math.max(selected.length, DEFAULT_STR_LENGTH) : selected.length
                patch(type === 'auto' ? { typeMode: 'auto', type: resolvedType, length } : { typeMode: undefined, type, value: undefined, length })
              }}
              choices={[{ value: 'auto', label: `Auto · ${selected.type === 'B' ? 'T/F' : selected.type}` }, ...valueTypes.map((t) => ({ value: t.key, label: t.key === 'B' ? 'T/F' : t.key }))]} />
          <div className="suffix-field">
            <NumberField aria-label="Length" integer min={1} max={maxElementsOf(selected.type)} value={selected.length} onCommit={(length) => patch({ length }, `${selected.id}:len`)} />
            <span>{selected.type === 'STR' ? 'chars' : 'items'}</span>
          </div>
        </div>
      </div>
    )}

    <div className="object-details-section">
      <h3>Properties</h3>
      <ChoiceField<string> aria-label="Location" value={w.selected?.parent?.id ?? ''} onChange={(next) => next !== undefined && w.edit((current) => moveObject(current, selected.id, next || null))}
          choices={[...(selected.kind !== 'reference' ? [{ value: '', label: 'Top level' }] : []), ...availableFolders.map((folder) => ({ value: folder.id, label: folder.name }))]} />
      <div className="object-details-move">
        <button disabled={w.selected?.index === 0} onClick={() => w.move(selected.id, -1)}>
          <ArrowUp />
          <span>Move up</span>
        </button>
        <button disabled={w.selected?.index === (w.selected?.parent?.children ?? w.project.objects).length - 1} onClick={() => w.move(selected.id, 1)}>
          <ArrowDown />
          <span>Move down</span>
        </button>
      </div>
      {selected.kind !== 'reference' && (
        <textarea aria-label="Description" placeholder="Description" value={selected.description ?? ''} onChange={(event) => patch({ description: event.target.value || undefined }, `${selected.id}:desc`)} />
      )}
      {selected.kind === 'value' && (
        <div className="object-details-toggles">
          <Switch checked={!selected.mutable} label="Read only" onChange={(checked) => patch({ mutable: !checked })} />
          <Switch checked={selected.retentive} label="Keep after restart" onChange={(checked) => patch({ retentive: checked })} />
          <Switch checked={selected.subscribed ?? false} label="Track live (subscribe)" onChange={(checked) => patch({ subscribed: checked })} />
        </div>
      )}
      {selected.kind === 'folder' && (
        <div className="object-details-toggles">
          <Switch checked={selected.subscribed ?? false} label="Track live (subscribe)" onChange={(checked) => patch({ subscribed: checked })} />
        </div>
      )}
    </div>

    <div className="object-details-section">
      <h3>Related folders</h3>
      {related.size
        ? [...related].map(([id, name]) => <button key={id} onClick={() => w.select(id)}><Folder />{name}<ChevronRight /></button>)
        : <p>No related folders.</p>}
    </div>
    {selected.kind === 'reference' && (
      <div className="object-details-section">
        <h3>Reference target</h3>
        <button onClick={() => w.select(selected.targetId)}><Link2 />{target?.name ?? 'Missing target'}<ChevronRight /></button>
      </div>
    )}
  </div>
}
