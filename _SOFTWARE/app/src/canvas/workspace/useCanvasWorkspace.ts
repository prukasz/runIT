import { useRef, useState } from 'react'
import { readStored, usePersistEffect, usePersistedFlag } from '../../hooks/useStorage'
import { addBlock, addCanvas, DEFAULT_VIEWPORT, findBlock, GRID, moveBlock, moveCanvas, newBlockId, pasteBlock, removeBlock, removeCanvas, renameCanvas, screenToCanvas, setCanvasDisabled, snapPoint, updateBlock } from '../../domain/canvas'
import type { Point, Viewport } from '../../domain/canvas'
import { runitVmCatalog } from '../../domain/descriptors'
import { parseCanvases } from '../../domain/project'
import type { CanvasBlock, CanvasVariableTarget, ProjectCanvas } from '../../domain/project'
import { useUndoHistory } from '../../hooks/useUndoHistory'
import { blockShape } from '../blocks/blockView'

/*
 * The Code view's canvases: the list (saved in the project file and
 * auto-saved to browser storage, edits with undo / redo like the other
 * workspaces), which one is open, and per canvas where it is scrolled and
 * zoomed (view state: not saved, not undone). Snap to grid is a preference.
 * Blocks are placed from the palette, moved, set up in the details panel and
 * removed; one block is selected at a time.
 */

const STORAGE_KEY = 'runit.canvases'
const SNAP_KEY = 'runit.canvas.snap'
const DETAIL_KEY = 'runit.canvas.detailed'

const newId = (): string => `canvas-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`

/** Split older shared labels into one free label per pin using that accessor. */
const splitSharedVariableLabels = (canvases: readonly ProjectCanvas[]): readonly ProjectCanvas[] => canvases.map((canvas) => {
  const usedIds = new Set((canvas.variables ?? []).map((variable) => variable.id))
  let suffix = 1
  const variables = (canvas.variables ?? []).flatMap((variable) => {
    if (variable.targets !== undefined) return [variable]
    const pathKey = JSON.stringify(variable.path)
    const targets: CanvasVariableTarget[] = canvas.blocks.flatMap((block) => [
      ...(block.inputs ?? []).flatMap((path, index) => path && JSON.stringify(path) === pathKey ? [{ block: block.id, kind: 'in' as const, index }] : []),
      ...(block.enables ?? []).flatMap((path, index) => JSON.stringify(path) === pathKey ? [{ block: block.id, kind: 'en' as const, index }] : []),
    ])
    if (targets.length <= 1) return [variable]
    return targets.map((target, index) => {
      let id = index === 0 ? variable.id : `${variable.id}-${index + 1}`
      while (usedIds.has(id) && id !== variable.id) id = `${variable.id}-${++suffix}`
      usedIds.add(id)
      return { ...variable, id, y: variable.y + index * GRID * 1.5, targets: [target] }
    })
  })
  return { ...canvas, variables }
})

const loadCanvases = (): readonly ProjectCanvas[] =>
  readStored<readonly ProjectCanvas[] | undefined>(STORAGE_KEY, (raw) => splitSharedVariableLabels(parseCanvases(JSON.parse(raw), 'canvases')), undefined) ?? addCanvas([], newId())

export function useCanvasWorkspace(onSelectBlock?: () => void) {
  const history = useUndoHistory(loadCanvases)
  const [activeId, setActiveId] = useState<string | undefined>(() => history.present[0]?.id)
  const [viewports, setViewports] = useState<ReadonlyMap<string, Viewport>>(new Map())
  const [snap, setSnap] = usePersistedFlag(SNAP_KEY, true)
  const [detailed, setDetailed] = usePersistedFlag(DETAIL_KEY, false)
  const [error, setError] = useState('')
  const [selectedBlockId, setSelectedBlockId] = useState<string>()
  /** Size of the canvas on screen (set by the surface): where the middle of the view is. */
  const surfaceSize = useRef({ width: 0, height: 0 })
  /** The block type being dragged from the palette (a drag shows only its data types until the drop). */
  const paletteDrag = useRef<string | undefined>(undefined)
  const canvases = history.present
  const active = canvases.find((canvas) => canvas.id === activeId) ?? canvases[0]

  usePersistEffect(STORAGE_KEY, history.present)

  /** Apply a change with undo; a thrown error is shown and nothing changes. */
  const edit = (change: (current: readonly ProjectCanvas[]) => readonly ProjectCanvas[]): boolean => {
    try {
      history.record(change(history.current()))
      setError('')
      return true
    } catch (cause) {
      setError(cause instanceof Error ? cause.message : String(cause))
      return false
    }
  }

  const create = () => {
    const id = newId()
    if (edit((current) => addCanvas(current, id))) setActiveId(id)
  }

  const remove = (id: string) => {
    const list = history.current()
    const index = list.findIndex((canvas) => canvas.id === id)
    if (!edit((current) => removeCanvas(current, id))) return
    // Open the neighbour that takes its place.
    if (active?.id === id) setActiveId((list[index + 1] ?? list[index - 1])?.id)
  }

  const selectBlock = (id: string | undefined) => {
    setSelectedBlockId(id)
    if (id) onSelectBlock?.()
  }

  /**
   * Place a new block of this type on the open canvas, and select it: at
   * `at` (a drop), or near the middle of the view, moved down past any block
   * it would cover (a click in the palette).
   */
  const placeBlock = (typeKey: string, at?: Point) => {
    if (!active) return
    let spot = at
    if (!spot) {
      const view = (active && viewports.get(active.id)) ?? DEFAULT_VIEWPORT
      const catalog = runitVmCatalog()
      const size = blockShape(catalog.block(typeKey), { id: '', type: typeKey }, detailed)
      const rects = active.blocks.map((block) => ({ ...blockShape(catalog.block(block.type), block, detailed), x: block.x, y: block.y }))
      spot = snapPoint(screenToCanvas(view, { x: surfaceSize.current.width / 2 - size.width / 2, y: surfaceSize.current.height / 3 }))
      for (let tries = 0; tries < 200; tries++) {
        const at: Point = spot
        const hit = rects.find((rect) => at.x < rect.x + rect.width && rect.x < at.x + size.width && at.y < rect.y + rect.height && rect.y < at.y + size.height)
        if (!hit) break
        spot = { x: at.x, y: hit.y + hit.height + GRID }
      }
    }
    const id = newBlockId(history.current(), typeKey)
    const place = spot
    if (edit((current) => addBlock(current, active.id, { id, type: typeKey, x: place.x, y: place.y }))) selectBlock(id)
  }

  // Copy / paste: the copied block, and how many times it was pasted (each paste steps down-right).
  const [clipboard, setClipboard] = useState<CanvasBlock>()
  const pasted = useRef(0)
  const copy = () => {
    const found = selectedBlockId ? findBlock(canvases, selectedBlockId) : undefined
    if (!found) return
    setClipboard(found.block)
    pasted.current = 0
  }
  const paste = () => {
    if (!clipboard || !active) return
    pasted.current += 1
    const at = snapPoint({ x: clipboard.x + 2 * GRID * pasted.current, y: clipboard.y + 2 * GRID * pasted.current })
    let id: string | undefined
    if (edit((current) => {
      const result = pasteBlock(current, active.id, clipboard, at)
      id = result.id
      return result.canvases
    }) && id) selectBlock(id)
  }

  const deleteBlock = (id: string) => {
    if (edit((current) => removeBlock(current, id)) && selectedBlockId === id) setSelectedBlockId(undefined)
  }

  /** Replace the canvases (project opened); undo goes back. */
  const load = (next: readonly ProjectCanvas[]) => {
    const normalized = splitSharedVariableLabels(next)
    history.replace([...normalized])
    setActiveId(normalized[0]?.id)
    setSelectedBlockId(undefined)
    setViewports(new Map())
    setError('')
  }

  return {
    canvases,
    active,
    select: (id: string | undefined) => {
      setActiveId(id)
      setSelectedBlockId(undefined)
    },
    create,
    rename: (id: string, name: string) => edit((current) => renameCanvas(current, id, name)),
    remove,
    move: (id: string, to: number) => edit((current) => moveCanvas(current, id, to)),
    setDisabled: (id: string, disabled: boolean) => edit((current) => setCanvasDisabled(current, id, disabled)),
    viewport: (active && viewports.get(active.id)) ?? DEFAULT_VIEWPORT,
    /** Whether this canvas has been shown yet in this session (else the surface centers it). */
    hasViewport: !!active && viewports.has(active.id),
    setViewport: (viewport: Viewport) => {
      if (active) setViewports((current) => new Map(current).set(active.id, viewport))
    },
    snap,
    setSnap,
    detailed,
    setDetailed,
    surfaceSize,
    paletteDrag,
    /** The selected block (on any canvas: it may have been undone away). */
    selectedBlock: selectedBlockId ? findBlock(canvases, selectedBlockId)?.block : undefined,
    selectBlock,
    placeBlock,
    moveBlock: (id: string, to: Point) => edit((current) => moveBlock(current, id, to)),
    updateBlock: (id: string, change: (block: CanvasBlock) => CanvasBlock) => edit((current) => updateBlock(current, id, change)),
    /** Edits the open canvas as a whole (its variable chips), one undo step. */
    updateActive: (change: (canvas: ProjectCanvas) => ProjectCanvas) => edit((current) => current.map((canvas) => (canvas.id === active?.id ? change(canvas) : canvas))),
    deleteBlock,
    /** Copy the selected block; paste puts a copy on the open canvas (one undo step). */
    copy,
    paste,
    canPaste: !!clipboard && !!active,
    error,
    clearError: () => setError(''),
    load,
    canUndo: history.canUndo,
    canRedo: history.canRedo,
    undo: history.undo,
    redo: history.redo,
  }
}

export type CanvasWorkspace = ReturnType<typeof useCanvasWorkspace>
