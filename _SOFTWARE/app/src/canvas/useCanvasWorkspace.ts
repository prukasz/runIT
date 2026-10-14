import { useRef, useState } from 'react'
import { readStored, usePersistEffect } from '../hooks/useStorage'
import { useUndoHistory } from '../hooks/useUndoHistory'
import { addBlock, addCanvas, addFreeChip, DEFAULT_VIEWPORT, dockFreeChip, findBlock, GRID, moveBlock, moveCanvas, moveFreeChip, newBlockId, pasteBlocks, removeBlock, removeCanvas, removeFreeChip, renameCanvas, screenToCanvas, setCanvasDisabled, snapPoint, updateBlock } from '../domain/canvas'
import type { CanvasBlock, ObjectPath, Point, ProjectCanvas, Viewport, WireTarget } from '../domain/canvas'
import { blockShape, descriptorOf } from '../blocks/blockModel'

/*
 * The canvases: the list (auto-saved to browser storage, edits with undo / redo), which one is open, and per canvas where it is
 * scrolled and zoomed (view state: not saved, not undone). Blocks are placed from the palette, moved, wired and removed; one block
 * is selected, or several (Shift / Ctrl, or a box drawn on the background) and then moved, copied and deleted together.
 */

const STORAGE_KEY = 'runit.canvases'

const newId = (): string => `canvas-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`

const parseCanvases = (raw: string): readonly ProjectCanvas[] | undefined => {
  const value: unknown = JSON.parse(raw)
  return Array.isArray(value) ? (value as ProjectCanvas[]) : undefined
}

const loadCanvases = (): readonly ProjectCanvas[] => readStored<readonly ProjectCanvas[] | undefined>(STORAGE_KEY, parseCanvases, undefined) ?? addCanvas([], newId())

export function useCanvasWorkspace() {
  const history = useUndoHistory(loadCanvases)
  const [activeId, setActiveId] = useState<string | undefined>(() => history.present[0]?.id)
  const [viewports, setViewports] = useState<ReadonlyMap<string, Viewport>>(new Map())
  const [error, setError] = useState('')
  /** The selected blocks' IDs, in the order they were picked. */
  const [selectedIds, setSelectedIds] = useState<readonly string[]>([])
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

  const selectBlock = (id: string | undefined) => setSelectedIds(id ? [id] : [])

  /** Add the block to the selection, or take it out when it is in. */
  const toggleBlock = (id: string) => setSelectedIds((current) => (current.includes(id) ? current.filter((entry) => entry !== id) : [...current, id]))

  /** Select these blocks: in addition to the ones selected, or instead of them. */
  const selectBlocks = (ids: readonly string[], add = false) => setSelectedIds((current) => (add ? [...current, ...ids.filter((id) => !current.includes(id))] : [...ids]))

  /**
   * Place a new block of this type on the open canvas, and select it: at `at` (a drop), or near the middle of the view, moved
   * down past any block it would cover (a click in the palette).
   */
  const placeBlock = (typeKey: string, at?: Point, settings?: Readonly<Record<string, number | string>>) => {
    if (!active) return
    let spot = at
    if (!spot) {
      const view = viewports.get(active.id) ?? DEFAULT_VIEWPORT
      const size = blockShape(descriptorOf(typeKey), { id: '', type: typeKey, ...(settings ? { settings } : {}) })
      const rects = active.blocks.map((block) => ({ ...blockShape(descriptorOf(block.type), block), x: block.x, y: block.y }))
      spot = snapPoint(screenToCanvas(view, { x: surfaceSize.current.width / 2 - size.width / 2, y: surfaceSize.current.height / 3 }))
      for (let tries = 0; tries < 200; tries++) {
        const here: Point = spot
        const hit = rects.find((rect) => here.x < rect.x + rect.width && rect.x < here.x + size.width && here.y < rect.y + rect.height && rect.y < here.y + size.height)
        if (!hit) break
        spot = { x: here.x, y: hit.y + hit.height + GRID }
      }
    }
    const id = newBlockId(history.current(), typeKey)
    const place = spot
    if (edit((current) => addBlock(current, active.id, { id, type: typeKey, x: place.x, y: place.y, ...(settings ? { settings } : {}) }))) selectBlock(id)
  }

  // Copy / paste: the copied blocks, and how many times they were pasted (each paste steps down-right).
  const [clipboard, setClipboard] = useState<readonly CanvasBlock[]>([])
  const pasted = useRef(0)
  const copy = () => {
    const found = selectedIds.flatMap((id) => findBlock(canvases, id)?.block ?? [])
    if (!found.length) return
    setClipboard(found)
    pasted.current = 0
  }
  const paste = () => {
    if (!clipboard.length || !active) return
    pasted.current += 1
    const step = 2 * GRID * pasted.current
    let ids: string[] = []
    if (edit((current) => {
      const result = pasteBlocks(current, active.id, clipboard, { x: step, y: step })
      ids = result.ids
      return result.canvases
    }) && ids.length) selectBlocks(ids)
  }

  const deleteBlocks = (ids: readonly string[]) => {
    if (edit((current) => ids.reduce((list, id) => (findBlock(list, id) ? removeBlock(list, id) : list), current))) setSelectedIds((now) => now.filter((id) => !ids.includes(id)))
  }

  /** Move blocks together by the same amount: one undo step. */
  const moveBlocks = (ids: readonly string[], by: Point) =>
    edit((current) => ids.reduce((list, id) => {
      const block = findBlock(list, id)?.block
      return block ? moveBlock(list, id, { x: block.x + by.x, y: block.y + by.y }) : list
    }, current))

  /** Change the open canvas itself (its free chips): one undo step. */
  const editActive = (change: (canvas: ProjectCanvas) => ProjectCanvas): boolean => {
    if (!active) return false
    return edit((current) => current.map((canvas) => (canvas.id === active.id ? change(canvas) : canvas)))
  }

  /** Replace every canvas (a project opened); an empty list gets one canvas. Undo goes back to the canvases before. */
  const load = (list: readonly ProjectCanvas[]) => {
    const next = list.length ? list : addCanvas([], newId())
    history.replace(next)
    setActiveId(next[0]?.id)
    setSelectedIds([])
    setViewports(new Map())
    setError('')
  }

  return {
    load,
    canvases,
    active,
    select: (id: string | undefined) => {
      setActiveId(id)
      setSelectedIds([])
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
    surfaceSize,
    paletteDrag,
    selectedIds,
    selectBlock,
    toggleBlock,
    selectBlocks,
    placeBlock,
    moveBlock: (id: string, to: Point) => edit((current) => moveBlock(current, id, to)),
    moveBlocks,
    updateBlock: (id: string, change: (block: CanvasBlock) => CanvasBlock) => edit((current) => updateBlock(current, id, change)),
    deleteBlocks,
    /** Free chips: a variable pulled off its pin (or dropped from the Variables tab) onto the canvas. */
    addFreeChip: (path: ObjectPath, at: Point) => editActive((canvas) => addFreeChip(canvas, path, at)),
    moveFreeChip: (id: string, at: Point) => editActive((canvas) => moveFreeChip(canvas, id, at)),
    removeFreeChip: (id: string) => editActive((canvas) => removeFreeChip(canvas, id)),
    dockFreeChip: (id: string, blockId: string, target: WireTarget) => editActive((canvas) => dockFreeChip(canvas, id, blockId, target)),
    copy,
    paste,
    canPaste: clipboard.length > 0 && !!active,
    error,
    clearError: () => setError(''),
    canUndo: history.canUndo,
    canRedo: history.canRedo,
    undo: history.undo,
    redo: history.redo,
  }
}

export type CanvasWorkspace = ReturnType<typeof useCanvasWorkspace>
