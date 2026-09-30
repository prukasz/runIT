import { useRef, useState } from 'react'
import { readStored, usePersistEffect, usePersistedFlag } from '../../hooks/useStorage'
import { addBlock, addCanvas, DEFAULT_VIEWPORT, findBlock, GRID, moveBlock, moveCanvas, newBlockId, pasteBlocks, removeBlock, removeCanvas, renameCanvas, screenToCanvas, setCanvasDisabled, snapPoint, updateBlock } from '../../domain/canvas'
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
 * removed; one block is selected, or several (tap them in multiple-select
 * mode, or with Shift / Ctrl, or draw a box) and then moved, copied and deleted together.
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
  /** The selected blocks' IDs, in the order they were picked; the inspector shows one block, or a summary of several. */
  const [selectedIds, setSelectedIds] = useState<readonly string[]>([])
  /** Multiple-select mode (the toolbar button): a tap adds or removes a block, dragging the background draws a box. */
  const [multiSelect, setMultiSelect] = useState(false)
  /** What the toolbar's trash button does: the surface registers it while a wire, variable chip or block is selected (touch has no Delete key). */
  const [deleteSelection, setDeleteSelection] = useState<(() => void) | undefined>()
  /** A block to bring to the middle of the view once its canvas is shown (the surface clears it). */
  const [focus, setFocus] = useState<{ canvasId: string; blockId: string }>()
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
    setSelectedIds(id ? [id] : [])
    if (id) onSelectBlock?.()
  }

  /** Add the block to the selection, or take it out when it is in. */
  const toggleBlock = (id: string) => {
    setSelectedIds((current) => (current.includes(id) ? current.filter((entry) => entry !== id) : [...current, id]))
    onSelectBlock?.()
  }

  /** Select these blocks: in addition to the ones selected, or instead of them. */
  const selectBlocks = (ids: readonly string[], add = false) => {
    setSelectedIds((current) => (add ? [...current, ...ids.filter((id) => !current.includes(id))] : [...ids]))
    if (ids.length) onSelectBlock?.()
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
  const deleteBlock = (id: string) => deleteBlocks([id])

  /** Move blocks together by the same amount: one undo step. */
  const moveBlocks = (ids: readonly string[], by: Point) =>
    edit((current) => ids.reduce((list, id) => {
      const block = findBlock(list, id)?.block
      return block ? moveBlock(list, id, { x: block.x + by.x, y: block.y + by.y }) : list
    }, current))

  /** Replace the canvases (project opened); undo goes back. */
  const load = (next: readonly ProjectCanvas[]) => {
    const normalized = splitSharedVariableLabels(next)
    history.replace([...normalized])
    setActiveId(normalized[0]?.id)
    setSelectedIds([])
    setViewports(new Map())
    setError('')
  }

  /** Open a block on its canvas: that canvas, the block selected and centered. */
  const reveal = (canvasId: string, blockId: string) => {
    setActiveId(canvasId)
    setSelectedIds([blockId])
    setFocus({ canvasId, blockId })
  }

  return {
    canvases,
    active,
    focus,
    clearFocus: () => setFocus(undefined),
    reveal,
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
    snap,
    setSnap,
    detailed,
    setDetailed,
    surfaceSize,
    paletteDrag,
    /** The selected block when exactly one is selected (on any canvas: it may have been undone away). */
    selectedBlock: selectedIds.length === 1 ? findBlock(canvases, selectedIds[0]!)?.block : undefined,
    /** Every selected block that still exists. */
    selectedBlocks: selectedIds.flatMap((id) => findBlock(canvases, id)?.block ?? []),
    selectedIds,
    multiSelect,
    deleteSelection,
    setDeleteSelection,
    setMultiSelect,
    selectBlock,
    toggleBlock,
    selectBlocks,
    placeBlock,
    moveBlock: (id: string, to: Point) => edit((current) => moveBlock(current, id, to)),
    moveBlocks,
    updateBlock: (id: string, change: (block: CanvasBlock) => CanvasBlock) => edit((current) => updateBlock(current, id, change)),
    /** Edits the open canvas as a whole (its variable chips), one undo step. */
    updateActive: (change: (canvas: ProjectCanvas) => ProjectCanvas) => edit((current) => current.map((canvas) => (canvas.id === active?.id ? change(canvas) : canvas))),
    deleteBlock,
    deleteBlocks,
    /** Copy the selected blocks; paste puts copies on the open canvas (one undo step). */
    copy,
    paste,
    canPaste: clipboard.length > 0 && !!active,
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
