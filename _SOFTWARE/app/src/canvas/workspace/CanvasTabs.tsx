import { useEffect, useRef, useState } from 'react'
import { InlineRename } from '../../components/InlineRename'
import { ChevronLeft, ChevronRight, Eye, EyeOff, MoreHorizontal, Pencil, Plus, Trash2 } from 'lucide-react'
import type { ProjectCanvas } from '../../domain/project'
import type { CanvasWorkspace } from './useCanvasWorkspace'

/*
 * The canvas list along the bottom: one tab per canvas, in execution order
 * (the number is its place in the cycle). Click opens, double-click renames,
 * drag reorders, the ⋯ menu (or right-click) renames, disables, moves and
 * deletes; + adds a canvas.
 */

export function CanvasTabs({ workspace }: { workspace: CanvasWorkspace }) {
  const { canvases, active } = workspace
  const [renaming, setRenaming] = useState<string>()
  const [menu, setMenu] = useState<{ id: string; left: number }>()
  const [dragging, setDragging] = useState<string>()
  const [drop, setDrop] = useState<{ id: string; after: boolean }>()
  const barRef = useRef<HTMLDivElement>(null)

  useEffect(() => {
    if (!menu) return
    const close = (event: Event) => {
      if (event instanceof KeyboardEvent && event.key !== 'Escape') return
      if (event instanceof PointerEvent && (event.target as HTMLElement).closest('.canvas-tab-menu')) return
      setMenu(undefined)
    }
    window.addEventListener('pointerdown', close)
    window.addEventListener('keydown', close)
    return () => {
      window.removeEventListener('pointerdown', close)
      window.removeEventListener('keydown', close)
    }
  }, [menu])

  const openMenu = (id: string, element: HTMLElement) => {
    const bar = barRef.current!.getBoundingClientRect()
    setMenu({ id, left: element.getBoundingClientRect().left - bar.left })
  }

  const remove = (canvas: ProjectCanvas) => {
    setMenu(undefined)
    if (canvas.blocks.length && !window.confirm(`Delete '${canvas.name}' and its ${canvas.blocks.length} block(s)?`)) return
    workspace.remove(canvas.id)
  }

  const onDrop = (target: ProjectCanvas) => {
    if (!dragging || !drop || dragging === target.id) return
    const from = canvases.findIndex((canvas) => canvas.id === dragging)
    let to = canvases.findIndex((canvas) => canvas.id === target.id) + (drop.after ? 1 : 0)
    if (from < to) to -= 1
    workspace.move(dragging, to)
  }

  const menuCanvas = menu && canvases.find((canvas) => canvas.id === menu.id)
  const menuIndex = menuCanvas ? canvases.indexOf(menuCanvas) : -1

  return (
    <div className="canvas-tabs" ref={barRef}>
      <div className="canvas-tab-list" role="tablist" aria-label="Canvases, in execution order" title="Every cycle runs the enabled canvases in this order">
        {canvases.map((canvas, index) => {
          const selected = canvas.id === active?.id
          const classes = ['canvas-tab', selected && 'selected', canvas.disabled && 'is-disabled', dragging === canvas.id && 'is-dragging', drop?.id === canvas.id && (drop.after ? 'drop-after' : 'drop-before')].filter(Boolean).join(' ')
          return (
            <div
              key={canvas.id}
              role="tab"
              aria-selected={selected}
              tabIndex={selected ? 0 : -1}
              className={classes}
              draggable={renaming !== canvas.id}
              title={canvas.disabled ? `${canvas.name}: disabled, left out of the program` : `${canvas.name}: runs ${index + 1}${index === 0 ? 'st' : index === 1 ? 'nd' : index === 2 ? 'rd' : 'th'} in each cycle`}
              onClick={() => workspace.select(canvas.id)}
              onDoubleClick={() => setRenaming(canvas.id)}
              onContextMenu={(event) => {
                event.preventDefault()
                workspace.select(canvas.id)
                openMenu(canvas.id, event.currentTarget)
              }}
              onKeyDown={(event) => {
                if (event.key === 'F2') setRenaming(canvas.id)
                if (event.key === 'ArrowLeft' || event.key === 'ArrowRight') workspace.select(canvases[index + (event.key === 'ArrowLeft' ? -1 : 1)]?.id ?? canvas.id)
              }}
              onDragStart={(event) => {
                setDragging(canvas.id)
                event.dataTransfer.effectAllowed = 'move'
              }}
              onDragOver={(event) => {
                if (!dragging) return
                event.preventDefault()
                const rect = event.currentTarget.getBoundingClientRect()
                setDrop({ id: canvas.id, after: event.clientX > rect.left + rect.width / 2 })
              }}
              onDragLeave={() => setDrop((current) => (current?.id === canvas.id ? undefined : current))}
              onDrop={(event) => {
                event.preventDefault()
                onDrop(canvas)
                setDragging(undefined)
                setDrop(undefined)
              }}
              onDragEnd={() => {
                setDragging(undefined)
                setDrop(undefined)
              }}
            >
              <span className="canvas-tab-order">{index + 1}</span>
              {canvas.disabled && <EyeOff className="canvas-tab-off" aria-label="disabled" />}
              {renaming === canvas.id ? (
                <InlineRename
                  className="canvas-tab-rename"
                  value={canvas.name}
                  label="Canvas name"
                  selectOnFocus
                  onCommit={(value) => {
                    if (value !== canvas.name) workspace.rename(canvas.id, value)
                    setRenaming(undefined)
                  }}
                  onCancel={() => setRenaming(undefined)}
                />
              ) : (
                <span className="canvas-tab-name">{canvas.name}</span>
              )}
              <button
                type="button"
                className="canvas-tab-more"
                aria-label={`${canvas.name} options`}
                title="Options"
                onClick={(event) => {
                  event.stopPropagation()
                  workspace.select(canvas.id)
                  if (menu?.id === canvas.id) setMenu(undefined)
                  else openMenu(canvas.id, event.currentTarget.parentElement!)
                }}
                onPointerDown={(event) => event.stopPropagation()}
              >
                <MoreHorizontal aria-hidden="true" />
              </button>
            </div>
          )
        })}
        <button type="button" className="canvas-tab-add" aria-label="New canvas" title="New canvas" onClick={workspace.create}><Plus aria-hidden="true" /></button>
      </div>
      {menuCanvas && (
        <div className="canvas-tab-menu" role="menu" style={{ left: menu.left }}>
          <button type="button" role="menuitem" onClick={() => { setMenu(undefined); setRenaming(menuCanvas.id) }}><Pencil aria-hidden="true" />Rename</button>
          <button type="button" role="menuitem" onClick={() => { setMenu(undefined); workspace.setDisabled(menuCanvas.id, !menuCanvas.disabled) }}>
            {menuCanvas.disabled ? <Eye aria-hidden="true" /> : <EyeOff aria-hidden="true" />}{menuCanvas.disabled ? 'Enable' : 'Disable (leave out of the program)'}
          </button>
          <button type="button" role="menuitem" disabled={menuIndex === 0} onClick={() => { setMenu(undefined); workspace.move(menuCanvas.id, menuIndex - 1) }}><ChevronLeft aria-hidden="true" />Run earlier</button>
          <button type="button" role="menuitem" disabled={menuIndex === canvases.length - 1} onClick={() => { setMenu(undefined); workspace.move(menuCanvas.id, menuIndex + 1) }}><ChevronRight aria-hidden="true" />Run later</button>
          <button type="button" role="menuitem" className="danger" onClick={() => remove(menuCanvas)}><Trash2 aria-hidden="true" />Delete</button>
        </div>
      )}
    </div>
  )
}
