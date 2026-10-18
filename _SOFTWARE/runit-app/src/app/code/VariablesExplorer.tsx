import { useState } from 'react'
import type { DragEvent } from 'react'
import { FolderPlus, Link2, Plus } from 'lucide-react'
import { NAME_MAX } from '@engine'
import type { Variable } from '@engine'
import { Button, InlineRename, PaletteSearch, TreeAction, TreeChildren, TreeDisclosure, TreeSlab, matchesSearch } from '@ui/components'
import type { CodeState } from './useCode'
import { TargetPill, VariableLead, useVariableErrors } from './VariableRow'
import { dropPlace } from './variables'
import './Code.css'

type DropMode = 'into' | 'before' | 'after'

/** Where over a row the pointer is: a folder takes a drop into its middle half, any row before its top and after its bottom. */
const modeAt = (event: DragEvent<HTMLDivElement>, folder: boolean): DropMode => {
  const rect = event.currentTarget.getBoundingClientRect()
  const y = (event.clientY - rect.top) / rect.height
  if (folder) return y < 0.25 ? 'before' : y > 0.75 ? 'after' : 'into'
  return y < 0.5 ? 'before' : 'after'
}

/**
 * The left panel of the Code view: the project's variables as a tree. A click selects (the centre shows it), a double click
 * renames, a drag moves (into a folder, before or after a row; on the empty space under the rows: to the top level). Search
 * opens the folders that hold a match.
 */
export function VariablesExplorer({ code }: { code: CodeState }) {
  const [query, setQuery] = useState('')
  const [renaming, setRenaming] = useState<string>()
  const [dragged, setDragged] = useState<string>()
  const [drop, setDrop] = useState<{ id: string; mode: DropMode }>()
  const boardErrors = useVariableErrors()
  const searching = query.trim() !== ''
  const matches = (v: Variable): boolean => matchesSearch(v.name, query)
  const holdsMatch = (v: Variable): boolean => v.kind === 'folder' && v.children.some((c) => matches(c) || holdsMatch(c))

  const endDrag = () => {
    setDragged(undefined)
    setDrop(undefined)
  }

  const rows = (list: readonly Variable[]) => list.map((v) => {
    if (searching && !matches(v) && !holdsMatch(v)) return null
    const folder = v.kind === 'folder'
    const open = folder && (searching ? holdsMatch(v) || !code.closed.has(v.id) : !code.closed.has(v.id))
    const at = drop?.id === v.id ? drop.mode : undefined
    return (
      <div key={v.id}>
        <TreeSlab
          icon={<VariableLead variable={v} code={code} />}
          label={v.name}
          labelEditor={renaming === v.id ? (
            <InlineRename value={v.name} label={`Name of ${v.name}`} maxLength={NAME_MAX} selectOnFocus
              onCommit={(text) => {
                setRenaming(undefined)
                const name = text.trim()
                if (name && name !== v.name) code.update(v.id, { name })
              }}
              onCancel={() => setRenaming(undefined)} />
          ) : undefined}
          refSlot={v.kind === 'reference' ? <TargetPill variable={v} code={code} /> : undefined}
          disclosure={folder ? <TreeDisclosure open={open} onToggle={() => code.toggle(v.id)} /> : undefined}
          actions={<TreeAction label={`Delete ${v.name}`} onClick={() => code.remove(v.id)} />}
          selected={code.selected?.variable.id === v.id}
          isFolder={folder}
          isReference={v.kind === 'reference'}
          twoRow={v.kind === 'reference'}
          isMatch={searching && matches(v)}
          isError={code.problems.some((p) => p.id === v.id) || (boardErrors.get(v.id) ?? 0) > 0}
          title={boardErrors.get(v.id) ? `${v.name}: ${boardErrors.get(v.id)} errors not read` : v.name}
          refKey={`variable:${v.id}`}
          isDragging={dragged === v.id}
          isDropInto={at === 'into'}
          isDropBefore={at === 'before'}
          isDropAfter={at === 'after'}
          draggable={renaming !== v.id}
          onDragStart={(event) => {
            event.stopPropagation()
            event.dataTransfer.setData('text/plain', v.id)
            event.dataTransfer.effectAllowed = 'move'
            setDragged(v.id)
          }}
          onDragEnd={endDrag}
          onDragOver={(event) => {
            if (dragged === undefined) return
            const mode = modeAt(event, folder)
            if (!dropPlace(code.tree, dragged, v.id, mode)) return
            event.preventDefault()
            event.stopPropagation()
            event.dataTransfer.dropEffect = 'move'
            if (drop?.id !== v.id || drop.mode !== mode) setDrop({ id: v.id, mode })
          }}
          onDragLeave={(event) => {
            if (!event.currentTarget.contains(event.relatedTarget as Node | null) && drop?.id === v.id) setDrop(undefined)
          }}
          onDrop={(event) => {
            event.preventDefault()
            event.stopPropagation()
            const place = dragged === undefined ? null : dropPlace(code.tree, dragged, v.id, modeAt(event, folder))
            if (dragged !== undefined && place) code.move(dragged, place.parent, place.index)
            endDrag()
          }}
          onClick={() => code.select(v.id)}
          onDoubleClick={() => setRenaming(v.id)}
        />
        {folder && <TreeChildren open={open}>{rows(v.children)}</TreeChildren>}
      </div>
    )
  })

  return (
    <div className="code-explorer">
      <div className="code-explorer-bar">
        <PaletteSearch value={query} onChange={setQuery} placeholder="Search" label="Search variables" />
        <Button size="sm" title="Add a value" aria-label="Add a value" onClick={() => code.add('value')}><Plus aria-hidden="true" /></Button>
        <Button size="sm" title="Add a folder" aria-label="Add a folder" onClick={() => code.add('folder')}><FolderPlus aria-hidden="true" /></Button>
        <Button size="sm" title="Add a reference to another variable" aria-label="Add a reference" onClick={() => code.add('reference')}><Link2 aria-hidden="true" /></Button>
      </div>
      <div className="code-tree" role="group" aria-label="Variables"
        onDragOver={(event) => {
          if (dragged === undefined) return
          event.preventDefault()
          setDrop(undefined)
        }}
        onDrop={(event) => {
          event.preventDefault()
          if (dragged !== undefined) code.move(dragged, null)
          endDrag()
        }}>
        {rows(code.tree)}
        {code.tree.length === 0 && <p className="code-hint">No variables yet.</p>}
      </div>
    </div>
  )
}
