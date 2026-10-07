import { useRef, useState } from 'react'
import { Download, Upload, X } from 'lucide-react'
import { PaletteSearch } from '../components/PaletteSearch'
import { PaletteSectionHeader } from '../components/PaletteSectionHeader'
import { TreeSlab } from '../components/TreeSlab'
import { Badge } from '../components/Badge'
import { Button } from '../components/Button'
import { ObjectTreePalette } from '../ObjectTreeWorkspace'
import type { ObjectWorkspace } from '../ObjectTreeWorkspace'
import { importedWidgetTypes, widgetType } from '../domain/controller'
import { useDebug } from '../debug/DebugContext'
import exampleWidget from '../../docs/controller/example.widget.js?raw'
import { WIDGET_DRAG_TYPE } from './drag'
import { widgetIcon } from './icons'
import type { ControllerWorkspace } from './useControllerWorkspace'
import './Controller.css'

/*
 * Left panel of the Remote view: like the Code view's, a Widgets list and the
 * Variables tree behind two tabs. A widget is dragged onto the grid, or clicked
 * to be placed on the first free spot (touch); a variable is dragged onto a widget.
 * Imported widgets (one .js file each) are listed under Custom.
 */

export const controllerPalettes = ['Widgets', 'Variables'] as const
export type ControllerPaletteName = typeof controllerPalettes[number]

const GROUPS: readonly { readonly title: string; readonly kinds: readonly string[]; readonly cat: string }[] = [
  { title: 'Controls', kinds: ['joystick', 'slider', 'switch', 'button', 'input', 'keyboard'], cat: 'io' },
  { title: 'Displays', kinds: ['display', 'chart', 'lamp', 'bar', 'gauge'], cat: 'data' },
  { title: 'Layout', kinds: ['label'], cat: 'flow' },
]

const matches = (query: string, kind: string): boolean => {
  const q = query.trim().toLowerCase()
  const type = widgetType(kind)
  return !q || [type.title, type.description, kind].some((text) => text.toLowerCase().includes(q))
}

const IMPORT_WARNING = 'An imported widget is code: it runs inside the app with everything the page can do, including the board connection.\n\nImport only a file you wrote or trust. Import it?'

const download = (name: string, text: string) => {
  const url = URL.createObjectURL(new Blob([text], { type: 'text/javascript' }))
  const link = document.createElement('a')
  link.href = url
  link.download = name
  link.click()
  URL.revokeObjectURL(url)
}

function WidgetList({ workspace, locked }: { workspace: ControllerWorkspace; locked: boolean }) {
  const [query, setQuery] = useState('')
  const [collapsed, setCollapsed] = useState<ReadonlySet<string>>(new Set())
  const [importProblem, setImportProblem] = useState<string>()
  const picker = useRef<HTMLInputElement>(null)
  const toggle = (title: string) => setCollapsed((current) => {
    const next = new Set(current)
    if (!next.delete(title)) next.add(title)
    return next
  })

  const importFile = async (file: File | undefined) => {
    if (!file) return
    setImportProblem(undefined)
    if (!window.confirm(IMPORT_WARNING)) return
    setImportProblem(await workspace.importWidget(file.name, await file.text()))
  }

  const row = (kind: string, cat: string, removable?: string) => {
    const type = widgetType(kind)
    const Icon = widgetIcon(kind)
    return (
      <TreeSlab
        key={kind}
        role="listitem"
        className={`block-palette-item controller-palette-item cat-${cat}`}
        disabled={locked}
        draggable={!locked}
        title={`${type.description}${type.keyboard ? '\nNeeds a keyboard: laptop only.' : ''}\n\nDrag onto the grid, or click to place it.`}
        ariaLabel={type.title}
        icon={<Icon aria-hidden="true" />}
        label={type.title}
        badges={type.keyboard ? <Badge size="compact" tone="info">laptop</Badge> : undefined}
        actions={removable && !locked ? <button type="button" aria-label={`Remove ${removable}`} title={`Remove the file ${removable}`} onClick={(event) => { event.stopPropagation(); workspace.removeImported(removable) }}><X aria-hidden="true" /></button> : undefined}
        onDragStart={(event) => {
          event.dataTransfer.setData(WIDGET_DRAG_TYPE, kind)
          event.dataTransfer.setData('text/plain', type.title)
          event.dataTransfer.effectAllowed = 'copy'
        }}
        onClick={() => workspace.add(kind)}
      />
    )
  }

  const custom = importedWidgetTypes().filter((type) => matches(query, type.kind))
  const errors = [...workspace.widgetErrors]
  const customOpen = !collapsed.has('Custom') || !!query
  return (
    <div className="block-palette">
      <PaletteSearch value={query} onChange={setQuery} placeholder="Search widgets..." label="Search widgets" />
      <div className="block-palette-scroll" role="list" aria-label="Widgets">
        {GROUPS.map((group) => {
          const kinds = group.kinds.filter((kind) => matches(query, kind))
          if (!kinds.length) return null
          const open = !collapsed.has(group.title) || !!query
          return (
            <div key={group.title} className="block-palette-group">
              <PaletteSectionHeader label={group.title} count={kinds.length} open={open} onToggle={() => toggle(group.title)} />
              {open && kinds.map((kind) => row(kind, group.cat))}
            </div>
          )
        })}
        <div className="block-palette-group">
          <PaletteSectionHeader label="Custom" count={custom.length} open={customOpen} onToggle={() => toggle('Custom')} />
          {customOpen && (
            <>
              {custom.map((type) => row(type.kind, 'system', type.file))}
              <div className="controller-import">
                <input ref={picker} type="file" accept=".js,text/javascript" hidden onChange={(event) => { void importFile(event.target.files?.[0]); event.target.value = '' }} />
                <Button size="sm" disabled={locked} onClick={() => picker.current?.click()} title="Add a widget from a .js file"><Upload aria-hidden="true" />Import widget</Button>
                <Button size="sm" onClick={() => download('example.widget.js', exampleWidget)} title="A working widget file to start from; the contract is in app/docs/controller-widgets.md"><Download aria-hidden="true" />Get template</Button>
              </div>
              {importProblem && <p className="controller-import-problem" role="alert">{importProblem}</p>}
              {errors.map(([file, problem]) => <p key={file} className="controller-import-problem" role="alert">{file}: {problem}</p>)}
            </>
          )}
        </div>
      </div>
      {locked && <p className="block-palette-note">The interface is enabled: disable it to change the layout.</p>}
    </div>
  )
}

export function ControllerPalette({ workspace, objects, palette, onPalette }: {
  workspace: ControllerWorkspace
  objects: ObjectWorkspace
  palette: ControllerPaletteName
  onPalette: (palette: ControllerPaletteName) => void
}) {
  // The layout is locked while the interface is enabled.
  const debug = useDebug()
  const locked = debug.active && !debug.stale
  return (
    <div className="code-palette" aria-label="Controller palette">
      <div className="code-palette-tabs" role="tablist" aria-label="Controller palettes">
        {controllerPalettes.map((name) => <button key={name} role="tab" aria-selected={palette === name} className={palette === name ? 'selected' : ''} onClick={() => onPalette(name)}>{name}</button>)}
      </div>
      <div className="code-palette-body" aria-label={`${palette} palette`}>
        {palette === 'Widgets' ? <WidgetList workspace={workspace} locked={locked} /> : <ObjectTreePalette workspace={objects} />}
      </div>
    </div>
  )
}
