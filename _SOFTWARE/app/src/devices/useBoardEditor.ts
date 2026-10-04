import { useEffect, useMemo, useRef, useState } from 'react'
import { cornerHoles, connectorType, freeSpot, MAX_BOARD_MM, MIN_BOARD_MM, newBoard, nextConnectorId, rotatedBy, validateBoard } from '../domain/board'
import type { BoardConnector, BoardTemplate, PinBinding } from '../domain/board'
import { usePersistedChoice } from '../hooks/useStorage'
import { buildBoardModel } from './board/boardModel'
import { libraryBoards, saveLibraryBoard } from './board/boardLibrary'
import { sysPinProviders } from './board/sysPins'
import type { DevicesWorkspace } from './useDevicesWorkspace'

/*
 * State and actions of the board creator (board-canvas-concept.md §9.11): the draft board being edited, what is
 * selected, the snap, and Save, which makes the draft the project's board and writes it to the board library. The
 * stage draws the draft, the palette adds connectors, the right panel edits the board, the connector and its pins.
 */

export const SNAP_STEPS = ['off', '0.25', '0.5', '1', '2.54'] as const
export type BoardSnap = (typeof SNAP_STEPS)[number]
const NUDGE_MM = 0.5

export interface BoardEditorStatus {
  readonly text: string
  readonly error?: boolean
}

export interface BoardSelection {
  readonly connector?: string
  /** 0-based pin of the selected connector. */
  readonly pin?: number
}

const slug = (text: string): string => {
  const base = text.toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^[^a-z]+/, '').replace(/_+$/, '')
  return base || 'board'
}

/** `active`: the creator is on screen (its keys work then). */
export function useBoardEditor(w: DevicesWorkspace, active: boolean) {
  const [draft, setDraft] = useState<BoardTemplate | undefined>(w.board)
  const [dirty, setDirty] = useState(false)
  const [selection, setSelection] = useState<BoardSelection>({})
  const [status, setStatus] = useState<BoardEditorStatus>()
  const [snap, setSnap] = usePersistedChoice<BoardSnap>('runit.boardeditor.snap', SNAP_STEPS, '0.5')
  const library = useMemo(() => libraryBoards(), [])

  // The project's board changed under the editor (a project opened, an undo): follow it unless there is something unsaved.
  useEffect(() => {
    if (!dirty) {
      setDraft(w.board)
      setSelection({})
    }
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [w.board])

  const model = useMemo(() => (draft ? buildBoardModel(draft, w.catalog) : undefined), [draft, w.catalog])
  const issues = useMemo(() => (draft ? validateBoard(draft) : []), [draft])
  const providers = useMemo(() => sysPinProviders(w.catalog, w.devices), [w.catalog, w.devices])

  const stepMm = snap === 'off' ? 0 : Number(snap)
  const snapped = (value: number): number => (stepMm ? Math.round(value / stepMm) * stepMm : value)

  /** Apply a change to the draft. */
  const edit = (change: (board: BoardTemplate) => BoardTemplate) => {
    setDraft((current) => (current ? change(current) : current))
    setDirty(true)
    setStatus(undefined)
  }
  const editConnector = (id: string, change: (connector: BoardConnector) => BoardConnector) =>
    edit((board) => ({ ...board, connectors: board.connectors.map((connector) => (connector.id === id ? change(connector) : connector)) }))

  const selected = draft?.connectors.find((connector) => connector.id === selection.connector)

  // ---- The board -----------------------------------------------------------------------------------------------

  const createBoard = (name = 'My board') => {
    const taken = new Set(library.map((entry) => entry.template.id))
    const base = slug(name)
    let id = base
    for (let n = 2; taken.has(id); n++) id = `${base}_${n}`
    setDraft(newBoard(id, name))
    setSelection({})
    setDirty(true)
    setStatus(undefined)
  }

  const openFromLibrary = (file: string) => {
    const entry = library.find((board) => board.file === file)
    if (!entry) return
    setDraft(entry.template)
    setSelection({})
    setDirty(true)
    setStatus({ text: `Opened ${entry.template.name} from the library: Save to use it in this project.` })
  }

  const setName = (name: string) => edit((board) => ({ ...board, name }))
  const setId = (id: string) => edit((board) => ({ ...board, id: id.trim().toLowerCase() }))
  const setSize = (width: number, height: number) =>
    edit((board) => {
      const w2 = Math.min(MAX_BOARD_MM, Math.max(MIN_BOARD_MM, width || board.width))
      const h2 = Math.min(MAX_BOARD_MM, Math.max(MIN_BOARD_MM, height || board.height))
      // Corner holes follow the corners while they are still the standard four.
      const corners = JSON.stringify(cornerHoles(board.width, board.height))
      return { ...board, width: w2, height: h2, holes: JSON.stringify(board.holes) === corners ? cornerHoles(w2, h2) : board.holes }
    })
  const setRadius = (radius: number) => edit((board) => ({ ...board, radius: Math.max(0, Math.min(50, radius || 0)) }))
  const setHoleDiameter = (holeDiameter: number) => edit((board) => ({ ...board, holeDiameter: Math.max(0, Math.min(20, holeDiameter || 0)) }))
  const setHoles = (preset: 'corners' | 'none') => edit((board) => ({ ...board, holes: preset === 'corners' ? cornerHoles(board.width, board.height) : [] }))

  // ---- Connectors ----------------------------------------------------------------------------------------------

  const addConnector = (typeId: string) => {
    const type = connectorType(typeId)
    if (!type || !draft) return
    const id = nextConnectorId(draft)
    const spot = freeSpot(draft, type, stepMm)
    const connector: BoardConnector = {
      id,
      type: typeId,
      x: spot.x,
      y: spot.y,
      rotation: 0,
      mirror: false,
      pins: type.pins.map(() => ({ bind: { type: 'none' } })),
    }
    edit((board) => ({ ...board, connectors: [...board.connectors, connector] }))
    setSelection({ connector: id })
  }

  const moveConnector = (id: string, x: number, y: number) => editConnector(id, (connector) => ({ ...connector, x: Number(snapped(x).toFixed(3)), y: Number(snapped(y).toFixed(3)) }))
  const nudge = (id: string, dx: number, dy: number) => editConnector(id, (connector) => ({ ...connector, x: Number((connector.x + dx).toFixed(3)), y: Number((connector.y + dy).toFixed(3)) }))
  const rotate = (id: string) => editConnector(id, (connector) => ({ ...connector, rotation: rotatedBy(connector.rotation) }))
  const mirror = (id: string) => editConnector(id, (connector) => ({ ...connector, mirror: !connector.mirror }))
  const setConnectorLabel = (id: string, label: string) => editConnector(id, (connector) => ({ ...connector, ...(label ? { label } : { label: undefined }) }))
  const setConnectorPosition = (id: string, x: number, y: number) => editConnector(id, (connector) => ({ ...connector, x, y }))
  const removeConnector = (id: string) => {
    edit((board) => ({ ...board, connectors: board.connectors.filter((connector) => connector.id !== id) }))
    setSelection({})
  }

  const setPinLabel = (id: string, index: number, label: string) =>
    editConnector(id, (connector) => ({ ...connector, pins: connector.pins.map((pin, at) => (at === index ? { ...pin, ...(label ? { label } : { label: undefined }), bind: pin.bind } : pin)) }))
  const setPinBinding = (id: string, index: number, bind: PinBinding) =>
    editConnector(id, (connector) => ({ ...connector, pins: connector.pins.map((pin, at) => (at === index ? { ...pin, bind } : pin)) }))

  // ---- Save ----------------------------------------------------------------------------------------------------

  const save = async () => {
    if (!draft) return
    const errors = validateBoard(draft).filter((issue) => issue.severity === 'error')
    if (errors.length) {
      setStatus({ text: errors[0].message, error: true })
      return
    }
    w.setBoard(draft)
    setDirty(false)
    setStatus({ text: `Saved ${draft.name} in the project.` })
    try {
      // The project is stored first: the dev page reloads when a library file changes.
      await new Promise((resolve) => window.setTimeout(resolve, 120))
      await saveLibraryBoard(draft)
      setStatus({ text: `Saved ${draft.name} in the project and the library.` })
    } catch {
      setStatus({ text: `Saved ${draft.name} in the project; the library is only reachable while the dev server runs.`, error: true })
    }
  }

  /** Back to the built-in runIT board for this project. */
  const useBuiltin = () => {
    w.setBoard(undefined)
    setDraft(undefined)
    setSelection({})
    setDirty(false)
    setStatus({ text: 'This project uses the built-in runIT board.' })
  }

  // ---- Keys: R / M turn the selected connector, Delete removes it, arrows nudge it ----------------------------------------

  const handler = useRef<(event: KeyboardEvent) => void>(() => {})
  handler.current = (event) => {
    const tag = (event.target as HTMLElement | null)?.tagName
    if (!selected || event.ctrlKey || event.metaKey || tag === 'INPUT' || tag === 'SELECT' || tag === 'TEXTAREA') return
    const step = event.shiftKey ? 2 : stepMm || NUDGE_MM
    if (event.key === 'r' || event.key === 'R') rotate(selected.id)
    else if (event.key === 'm' || event.key === 'M') mirror(selected.id)
    else if (event.key === 'Delete' || event.key === 'Backspace') removeConnector(selected.id)
    else if (event.key === 'ArrowLeft') nudge(selected.id, -step, 0)
    else if (event.key === 'ArrowRight') nudge(selected.id, step, 0)
    else if (event.key === 'ArrowUp') nudge(selected.id, 0, -step)
    else if (event.key === 'ArrowDown') nudge(selected.id, 0, step)
    else return
    event.preventDefault()
  }
  useEffect(() => {
    if (!active) return
    const listener = (event: KeyboardEvent) => handler.current(event)
    window.addEventListener('keydown', listener)
    return () => window.removeEventListener('keydown', listener)
  }, [active])

  return {
    draft, model, dirty, issues, status, setStatus,
    selection, setSelection, selected,
    snap, setSnap, stepMm, snapped,
    providers, library,
    createBoard, openFromLibrary, useBuiltin, save,
    setName, setId, setSize, setRadius, setHoleDiameter, setHoles,
    addConnector, moveConnector, setConnectorPosition, nudge, rotate, mirror, setConnectorLabel, removeConnector,
    setPinLabel, setPinBinding,
  }
}

export type BoardEditorApi = ReturnType<typeof useBoardEditor>
