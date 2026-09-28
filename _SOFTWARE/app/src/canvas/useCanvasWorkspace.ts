import { useEffect, useRef, useState } from 'react'
import { addBlock, addCanvas, DEFAULT_VIEWPORT, findBlock, GRID, moveBlock, moveCanvas, newBlockId, pasteBlock, removeBlock, removeCanvas, renameCanvas, screenToCanvas, setCanvasDisabled, snapPoint, updateBlock } from '../domain/canvas'
import type { Point, Viewport } from '../domain/canvas'
import { runitVmCatalog } from '../domain/descriptors'
import { parseCanvases } from '../domain/project'
import type { CanvasBlock, ProjectCanvas } from '../domain/project'
import { blockShape } from './blockView'

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

interface History {
  readonly past: readonly (readonly ProjectCanvas[])[]
  readonly present: readonly ProjectCanvas[]
  readonly future: readonly (readonly ProjectCanvas[])[]
}

const newId = (): string => `canvas-${Date.now().toString(36)}-${Math.random().toString(36).slice(2, 6)}`

const loadCanvases = (): readonly ProjectCanvas[] => {
  try {
    const raw = localStorage.getItem(STORAGE_KEY)
    if (raw) return parseCanvases(JSON.parse(raw), 'canvases')
  } catch {
    /* Storage unavailable or unreadable: start with one canvas. */
  }
  return addCanvas([], newId())
}

const loadSnap = (): boolean => {
  try {
    return localStorage.getItem(SNAP_KEY) !== 'off'
  } catch {
    return true
  }
}

const loadDetailed = (): boolean => {
  try { return localStorage.getItem(DETAIL_KEY) === 'on' } catch { return false }
}

export function useCanvasWorkspace(onSelectBlock?: () => void) {
  const [history, setHistory] = useState<History>(() => ({ past: [], present: loadCanvases(), future: [] }))
  const historyRef = useRef(history)
  const commit = (next: History) => {
    historyRef.current = next
    setHistory(next)
  }
  const [activeId, setActiveId] = useState<string | undefined>(() => history.present[0]?.id)
  const [viewports, setViewports] = useState<ReadonlyMap<string, Viewport>>(new Map())
  const [snap, setSnapState] = useState(loadSnap)
  const [detailed, setDetailedState] = useState(loadDetailed)
  const [error, setError] = useState('')
  const [selectedBlockId, setSelectedBlockId] = useState<string>()
  /** Size of the canvas on screen (set by the surface): where the middle of the view is. */
  const surfaceSize = useRef({ width: 0, height: 0 })
  /** The block type being dragged from the palette (a drag shows only its data types until the drop). */
  const paletteDrag = useRef<string | undefined>(undefined)
  const canvases = history.present
  const active = canvases.find((canvas) => canvas.id === activeId) ?? canvases[0]

  useEffect(() => {
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(history.present))
    } catch {
      /* The project file keeps them. */
    }
  }, [history.present])

  /** Apply a change with undo; a thrown error is shown and nothing changes. */
  const edit = (change: (current: readonly ProjectCanvas[]) => readonly ProjectCanvas[]): boolean => {
    const curr = historyRef.current
    try {
      const next = change(curr.present)
      commit({ past: [...curr.past, curr.present].slice(-50), present: next, future: [] })
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
    const list = historyRef.current.present
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
    const id = newBlockId(historyRef.current.present, typeKey)
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
    const curr = historyRef.current
    commit({ past: [...curr.past, curr.present], present: [...next], future: [] })
    setActiveId(next[0]?.id)
    setSelectedBlockId(undefined)
    setViewports(new Map())
    setError('')
  }

  const undo = () => {
    const curr = historyRef.current
    if (!curr.past.length) return
    commit({ past: curr.past.slice(0, -1), present: curr.past.at(-1)!, future: [curr.present, ...curr.future] })
  }
  const redo = () => {
    const curr = historyRef.current
    if (!curr.future.length) return
    commit({ past: [...curr.past, curr.present], present: curr.future[0]!, future: curr.future.slice(1) })
  }

  const setSnap = (on: boolean) => {
    setSnapState(on)
    try {
      localStorage.setItem(SNAP_KEY, on ? 'on' : 'off')
    } catch {
      /* A preference only. */
    }
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
    setViewport: (viewport: Viewport) => {
      if (active) setViewports((current) => new Map(current).set(active.id, viewport))
    },
    snap,
    setSnap,
    detailed,
    setDetailed: (on: boolean) => {
      setDetailedState(on)
      try { localStorage.setItem(DETAIL_KEY, on ? 'on' : 'off') } catch { /* A preference only. */ }
    },
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
    canUndo: history.past.length > 0,
    canRedo: history.future.length > 0,
    undo,
    redo,
  }
}

export type CanvasWorkspace = ReturnType<typeof useCanvasWorkspace>
