import type { DeviceCatalog } from '../../domain/descriptors'
import { useEffect, useLayoutEffect, useRef, useState } from 'react'
import type { CSSProperties } from 'react'
import { Crosshair, Minus, Plus } from 'lucide-react'
import { Button } from '../../components/Button'
import { canvasToScreen, chipLabel, connect, disconnect, GRID, kindOfDrag, OBJECT_DRAG_TYPE, pathLabel, screenToCanvas, snapPoint, sourceKind, sourceOf, sourcePath, variableKind, zoomAt } from '../../domain/canvas'
import type { Point, Viewport, Wire, WireSource, WireTarget } from '../../domain/canvas'
import { findObject } from '../../domain/project'
import type { CanvasBlock, ObjectPath, ProjectDocument } from '../../domain/project'
import type { Diagnostic } from '../../domain/compiler'
import type { ProjectDevice } from '../../domain/project'
import { DEFAULT_ENO, runitVmCatalog } from '../../domain/descriptors'
import { useDebug } from '../../debug/DebugContext'
import { BLOCK_DRAG_TYPE } from '../workspace/BlockPalette'
import { CanvasBlockView } from '../blocks/CanvasBlockView'
import { CanvasGroups } from './CanvasGroups'
import { CanvasWires, chipAtTarget, CHIP_HEIGHT, CHIP_WIDTH, FreeChip, LinkWindows, pickerRows, WirePicker } from './CanvasWiring'
import type { Pending, PickerRow, SourceChoice } from './CanvasWiring'
import { blockShape } from '../blocks/blockView'
import type { CanvasWorkspace } from '../workspace/useCanvasWorkspace'

/*
 * The open canvas: a grid that pans (drag the background, one finger) and
 * zooms (wheel, pinch, the buttons) around the pointer, and its blocks in
 * the world layer, which carries the viewport transform. Blocks dropped from
 * the palette show where they will land first; with snap on, the grid point
 * the pointer is nearest is marked. A click on the background clears the
 * selection; Delete removes the selected blocks. Several blocks are selected by
 * tapping them in multiple-select mode (or with Shift / Ctrl), or by drawing a box
 * on the background (that mode, or Shift); a dragged selected block takes the others along.
 */

/** Every MAJOR-th grid line is drawn stronger. */
const MAJOR = 5
const WHEEL_ZOOM = 0.0015
const BUTTON_ZOOM = 1.25
/** Where a dropped block's corner sits relative to the pointer: the pointer holds its header. */
const GRAB = { x: 24, y: 14 }
/** A press that moves less than this is a click. */
const CLICK_SLOP = 4

const isEditing = (target: EventTarget | null): boolean => target instanceof HTMLElement && (target.isContentEditable || !!target.closest('input, textarea, select, [contenteditable="true"]'))

export function CanvasSurface({
  workspace,
  showGrid,
  diagnostics,
  devices = [],
  deviceCatalog,
  project,
  selectedObjectId,
  onClearSelectedObject,
}: {
  workspace: CanvasWorkspace
  showGrid: boolean
  diagnostics: ReadonlyMap<string, readonly Diagnostic[]>
  devices?: readonly ProjectDevice[]
  deviceCatalog?: DeviceCatalog
  project?: ProjectDocument
  selectedObjectId?: string
  onClearSelectedObject?: () => void
}) {
  const { active, snap, selectedBlock, selectedIds } = workspace
  const debug = useDebug()
  const catalog = runitVmCatalog()
  const selectedObjectNode = selectedObjectId && project ? findObject(project, selectedObjectId)?.node : undefined
  const isLinking = Boolean(selectedObjectNode)
  const [ghost, setGhost] = useState<{ at: Point; type?: string }>()
  const pressAt = useRef<Point | undefined>(undefined)
  const surfaceRef = useRef<HTMLDivElement>(null)
  // The viewport lives here while it moves; the workspace keeps it per canvas.
  const [viewport, setViewportState] = useState<Viewport>(workspace.viewport)
  const viewportRef = useRef(viewport)
  const setViewport = (next: Viewport) => {
    viewportRef.current = next
    setViewportState(next)
  }
  const pointers = useRef(new Map<number, Point>())
  const [panning, setPanning] = useState(false)
  const [cursor, setCursor] = useState<Point>()
  const saveTimer = useRef<number | undefined>(undefined)
  // A wire or a variable being connected: where the pointer is, the block under it, the picker row under it.
  const [draft, setDraft] = useState<{ pending: Pending; pair?: { readonly from: string; readonly to: string; readonly source: number }; at: Point; over?: string; hovered?: string; picking?: boolean; from?: Point; ghost?: string }>()
  const [selectedWire, setSelectedWire] = useState<Wire>()
  // A press on a chip: docked (click opens the block, a drag pulls it off) or free (a drag moves it).
  const [chipPress, setChipPress] = useState<{ kind: 'docked'; target: WireTarget; path: ObjectPath; start: Point } | { kind: 'free'; id: string; start: Point; origin: Point; at: Point }>()
  const [selectedChip, setSelectedChip] = useState<string>()
  // A docked chip clicked: Delete takes the variable off its pin.
  const [selectedPinChip, setSelectedPinChip] = useState<WireTarget>()
  // A box being drawn on the background (screen points), and how far the dragged block of a selected group has moved.
  const [marquee, setMarquee] = useState<{ from: Point; to: Point }>()
  const [groupDrag, setGroupDrag] = useState<{ from: string; by: Point }>()
  // A press on a selected block waits to see whether it becomes a drag (the group moves) or a tap (the block leaves the group, or is all that stays selected).
  const tapAction = useRef<{ id: string; kind: 'toggle' | 'only' } | undefined>(undefined)

  // Another canvas opened: show where it was left; one not shown yet starts centered on its blocks (or on the origin when empty).
  useLayoutEffect(() => {
    if (workspace.hasViewport) {
      setViewport(workspace.viewport)
      return
    }
    const surface = surfaceRef.current
    if (!surface) return
    const blocks = active?.blocks ?? []
    let middle = { x: 0, y: 0 }
    if (blocks.length) {
      const boxes = blocks.map((block) => ({ block, shape: shapeOf(block) }))
      const left = Math.min(...boxes.map(({ block }) => block.x))
      const top = Math.min(...boxes.map(({ block }) => block.y))
      const right = Math.max(...boxes.map(({ block, shape }) => block.x + shape.width))
      const bottom = Math.max(...boxes.map(({ block, shape }) => block.y + shape.height))
      middle = { x: (left + right) / 2, y: (top + bottom) / 2 }
    }
    setViewport({ zoom: 1, x: Math.round(surface.clientWidth / 2 - middle.x), y: Math.round(surface.clientHeight / 2 - middle.y) })
    save()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [active?.id, workspace.hasViewport])

  // A block asked for (from a variable's connected blocks): its canvas is open, bring it to the middle.
  useLayoutEffect(() => {
    const focus = workspace.focus
    const surface = surfaceRef.current
    if (!focus || !surface || focus.canvasId !== active?.id) return
    const block = active.blocks.find((entry) => entry.id === focus.blockId)
    if (block) {
      const shape = shapeOf(block)
      const zoom = viewportRef.current.zoom
      setViewport({ zoom, x: Math.round(surface.clientWidth / 2 - (block.x + shape.width / 2) * zoom), y: Math.round(surface.clientHeight / 2 - (block.y + shape.height / 2) * zoom) })
      save()
    }
    workspace.clearFocus()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [workspace.focus, active?.id])

  // The middle of the view, for blocks placed by a click in the palette.
  useEffect(() => {
    const surface = surfaceRef.current
    if (!surface || typeof ResizeObserver === 'undefined') return
    const observer = new ResizeObserver(() => { workspace.surfaceSize.current = { width: surface.clientWidth, height: surface.clientHeight } })
    observer.observe(surface)
    return () => observer.disconnect()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  // Delete removes the selected block, Escape clears the selection (not while typing in a field).
  useEffect(() => {
    if (!selectedIds.length) return
    const onKey = (event: KeyboardEvent) => {
      if (isEditing(event.target) || event.ctrlKey || event.metaKey || event.altKey) return
      if (event.key === 'Delete' || event.key === 'Backspace') {
        event.preventDefault()
        workspace.deleteBlocks(selectedIds)
      } else if (event.key === 'Escape') workspace.selectBlock(undefined)
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [selectedIds.join('\n')])

  // Ctrl / Cmd + C copies the selected block, + V pastes it (not while typing in a field).
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (isEditing(event.target) || !(event.ctrlKey || event.metaKey) || event.altKey || event.shiftKey) return
      const key = event.key.toLowerCase()
      if (key === 'c' && workspace.selectedIds.length) {
        if (window.getSelection()?.toString()) return
        event.preventDefault()
        workspace.copy()
      } else if (key === 'v' && workspace.canPaste) {
        event.preventDefault()
        workspace.paste()
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  })

  // Delete removes the selected wire; Escape drops it, or a connection in progress, or clears selected variable.
  useEffect(() => {
    if (!selectedWire && !draft && !selectedChip && !selectedPinChip && !selectedObjectId) return
    const onKey = (event: KeyboardEvent) => {
      if (isEditing(event.target)) return
      if (event.key === 'Escape') {
        setDraft(undefined)
        setSelectedWire(undefined)
        setSelectedChip(undefined)
        setSelectedPinChip(undefined)
        onClearSelectedObject?.()
      } else if (event.key === 'Delete' || event.key === 'Backspace') {
        if (selectedWire) workspace.updateBlock(selectedWire.to.block, (block) => disconnect(block, selectedWire.to, selectedWire.enableIndex))
        else if (selectedPinChip) workspace.updateBlock(selectedPinChip.block, (block) => disconnect(block, selectedPinChip))
        // A free chip goes back onto its pins (they keep reading the variable).
        else if (selectedChip) workspace.updateActive((canvas) => ({ ...canvas, variables: (canvas.variables ?? []).filter((entry) => entry.id !== selectedChip) }))
        else return
        event.preventDefault()
        event.stopImmediatePropagation()
        setSelectedWire(undefined)
        setSelectedChip(undefined)
        setSelectedPinChip(undefined)
      }
    }
    window.addEventListener('keydown', onKey, true)
    return () => window.removeEventListener('keydown', onKey, true)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [selectedWire, draft, selectedChip, selectedPinChip, selectedObjectId])

  const save = (delay = 0) => {
    window.clearTimeout(saveTimer.current)
    saveTimer.current = window.setTimeout(() => workspace.setViewport(viewportRef.current), delay)
  }

  const local = (clientX: number, clientY: number): Point => {
    const rect = surfaceRef.current!.getBoundingClientRect()
    return { x: clientX - rect.left, y: clientY - rect.top }
  }

  // Wheel zoom needs a non-passive listener to keep the page from scrolling.
  useEffect(() => {
    const surface = surfaceRef.current
    if (!surface) return
    const onWheel = (event: WheelEvent) => {
      event.preventDefault()
      setViewport(zoomAt(viewportRef.current, Math.exp(-event.deltaY * WHEEL_ZOOM), local(event.clientX, event.clientY)))
      save(200)
    }
    surface.addEventListener('wheel', onWheel, { passive: false })
    return () => surface.removeEventListener('wheel', onWheel)
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [active?.id])

  const onPointerDown = (event: React.PointerEvent<HTMLDivElement>) => {
    if (event.button !== 0 && event.button !== 1) return
    // Blocks, the zoom buttons and messages handle their own presses.
    if ((event.target as HTMLElement).closest('.canvas-block, .canvas-zoom, .canvas-error, .canvas-empty, .wire-picker')) return
    setDraft(undefined)
    setSelectedWire(undefined)
    setSelectedChip(undefined)
    setSelectedPinChip(undefined)
    onClearSelectedObject?.()
    event.currentTarget.setPointerCapture(event.pointerId)
    const point = local(event.clientX, event.clientY)
    pointers.current.set(event.pointerId, point)
    pressAt.current = pointers.current.size === 1 ? point : undefined
    // Multiple-select mode (or Shift): one finger draws a box instead of panning; a second finger still pinches.
    if (pointers.current.size === 1 && (workspace.multiSelect || event.shiftKey)) setMarquee({ from: point, to: point })
    else setPanning(true)
  }

  const onPointerMove = (event: React.PointerEvent<HTMLDivElement>) => {
    const point = local(event.clientX, event.clientY)
    if (event.pointerType === 'mouse') setCursor(point)
    if (chipPress) {
      movedChip(event.clientX, event.clientY)
      return
    }
    if (draft && !draft.picking) {
      track(draft.pending, event.clientX, event.clientY)
      return
    }
    const previous = pointers.current.get(event.pointerId)
    if (!previous) return
    const others = [...pointers.current].filter(([id]) => id !== event.pointerId).map(([, at]) => at)
    pointers.current.set(event.pointerId, point)
    if (marquee) {
      if (others.length === 0) {
        setMarquee({ from: marquee.from, to: point })
        return
      }
      setMarquee(undefined)
      setPanning(true)
    }
    const current = viewportRef.current
    if (others.length === 1) {
      // Pinch: zoom by the change in finger distance around their midpoint, pan by the midpoint's move.
      const other = others[0]!
      const before = Math.hypot(previous.x - other.x, previous.y - other.y)
      const after = Math.hypot(point.x - other.x, point.y - other.y)
      const middle = { x: (point.x + other.x) / 2, y: (point.y + other.y) / 2 }
      const zoomed = before > 0 ? zoomAt(current, after / before, middle) : current
      setViewport({ ...zoomed, x: zoomed.x + (point.x - previous.x) / 2, y: zoomed.y + (point.y - previous.y) / 2 })
    } else if (others.length === 0) {
      setViewport({ ...current, x: current.x + point.x - previous.x, y: current.y + point.y - previous.y })
    }
  }

  const onPointerEnd = (event: React.PointerEvent<HTMLDivElement>) => {
    if (chipPress) {
      releasedChip(event.clientX, event.clientY)
      return
    }
    if (draft && !draft.picking) {
      finish(event.clientX, event.clientY)
      return
    }
    const press = pressAt.current
    if (press && pointers.current.size === 1 && pointers.current.has(event.pointerId)) {
      const end = local(event.clientX, event.clientY)
      if (Math.hypot(end.x - press.x, end.y - press.y) < CLICK_SLOP) workspace.selectBlock(undefined)
      else if (marquee) selectInside(marquee.from, end)
    }
    pressAt.current = undefined
    pointers.current.delete(event.pointerId)
    setMarquee(undefined)
    if (!pointers.current.size) {
      setPanning(false)
      save()
    }
  }

  /** The blocks a box drawn on the screen touches join the selection. */
  const selectInside = (a: Point, b: Point) => {
    const from = screenToCanvas(viewportRef.current, { x: Math.min(a.x, b.x), y: Math.min(a.y, b.y) })
    const to = screenToCanvas(viewportRef.current, { x: Math.max(a.x, b.x), y: Math.max(a.y, b.y) })
    const inside = (active?.blocks ?? []).filter((block) => {
      const shape = shapeOf(block)
      return block.x < to.x && block.x + shape.width > from.x && block.y < to.y && block.y + shape.height > from.y
    })
    workspace.selectBlocks(inside.map((block) => block.id), true)
  }

  const zoomButton = (factor: number) => {
    const rect = surfaceRef.current!.getBoundingClientRect()
    setViewport(zoomAt(viewportRef.current, factor, { x: rect.width / 2, y: rect.height / 2 }))
    save()
  }

  const recenter = () => {
    const rect = surfaceRef.current!.getBoundingClientRect()
    setViewport({ zoom: 1, x: Math.round(rect.width / 2 / GRID) * GRID, y: Math.round(rect.height / 2 / GRID) * GRID })
    save()
  }

  const cell = GRID * viewport.zoom
  // Dots on the grid points (each layer shifted half a tile so a dot sits on the origin); minor dots drop out when they would crowd.
  const layers = [{ size: cell * MAJOR, color: 'var(--canvas-dot-major)', radius: 1.4 }, ...(cell >= 8 ? [{ size: cell, color: 'var(--canvas-dot)', radius: 1 }] : [])]
  const gridStyle: CSSProperties = showGrid
    ? {
        backgroundImage: layers.map((layer) => `radial-gradient(circle, ${layer.color} ${layer.radius}px, transparent ${layer.radius + 0.4}px)`).join(', '),
        backgroundSize: layers.map((layer) => `${layer.size}px ${layer.size}px`).join(', '),
        backgroundPosition: layers.map((layer) => `${viewport.x - layer.size / 2}px ${viewport.y - layer.size / 2}px`).join(', '),
      }
    : {}

  const pointed = cursor && screenToCanvas(viewport, cursor)
  const snapped = pointed && (snap ? snapPoint(pointed) : pointed)
  const marker = snapped && canvasToScreen(viewport, snapped)
  const empty = active && active.blocks.length === 0

  // Connecting: the block under a canvas point (topmost), the picker row under the pointer.
  const shapeOf = (block: CanvasBlock) => blockShape(catalog.block(block.type), block, workspace.detailed)
  const blockAt = (point: Point, except?: string): CanvasBlock | undefined =>
    [...(active?.blocks ?? [])].reverse().find((block) => {
      if (block.id === except) return false
      const shape = shapeOf(block)
      return point.x >= block.x - 12 && point.x <= block.x + shape.width + 12 && point.y >= block.y && point.y <= block.y + shape.height
    })
  const rowAt = (clientX: number, clientY: number): string | undefined =>
    (document.elementFromPoint(clientX, clientY)?.closest('[data-wire-target]') as HTMLElement | null)?.dataset.wireTarget
  const track = (pending: Pending, clientX: number, clientY: number) => {
    const at = screenToCanvas(viewportRef.current, local(clientX, clientY))
    const over = blockAt(at, pending.kind === 'wire' ? pending.from.block : undefined)
    const hovered = rowAt(clientX, clientY)
    // Anywhere on the picker (it sticks out of the block, and its header and gaps are no row) the block stays chosen.
    const onPicker = !!document.elementFromPoint(clientX, clientY)?.closest('.wire-picker')
    setDraft((current) => ({ pending, at, ...(current?.from ? { from: current.from } : {}), ...(current?.ghost ? { ghost: current.ghost } : {}), ...(current?.over && (onPicker || (!over && hovered)) ? { over: current.over } : over ? { over: over.id } : {}), ...(hovered ? { hovered } : {}) }))
  }
  const rowsFor = (blockId: string | undefined, pending: Pending): PickerRow[] => {
    const block = active?.blocks.find((entry) => entry.id === blockId)
    return block ? pickerRows(block, catalog.block(block.type), shapeOf(block), pending, (path) => pathLabel(path, project, active?.blocks)) : []
  }
  /** What a block can send on: its ENO first (a FOR's puts the other block in its loop), then its outputs. */
  const sourcesOf = (from: CanvasBlock): { choice: SourceChoice; pending: Pending }[] => [
    {
      choice: { key: 'eno', label: (catalog.block(from.type)?.eno ?? DEFAULT_ENO).title, detail: (catalog.block(from.type)?.eno ?? DEFAULT_ENO).description, type: 'gate' },
      pending: { kind: 'wire', from: { block: from.id, pin: 'eno' }, carries: 'bool', ...(from.type === 'FOR' ? { loop: true } : {}) } as Pending,
    },
    ...shapeOf(from).outputs.map((pin, index) => ({
      choice: { key: `q${index}`, label: pin.title, detail: 'value', type: pin.value },
      pending: { kind: 'wire', from: { block: from.id, pin: `q${index}` }, carries: sourceKind(`q${index}`, pin.value) } as Pending,
    })),
  ]
  /** Which source is chosen first: a value if there is one, else the ENO. */
  const firstSource = (list: readonly { choice: SourceChoice }[]) => Math.max(0, list.findIndex((source) => source.choice.key !== 'eno'))
  /** The sources of `from` that have at least one pin to go to on `into`. */
  const fittingSources = (from: CanvasBlock, into: CanvasBlock) =>
    sourcesOf(from).filter((source) => rowsFor(into.id, source.pending).some((row) => row.ok))
  const pick = (row: PickerRow, pending: Pending) => {
    setDraft(undefined)
    if (!row.ok) return
    if (pending.kind === 'wire') {
      workspace.updateBlock(row.target.block, (block) => {
        const previous = row.target.kind === 'in' ? sourceOf(block.inputs?.[row.target.index], blocksById) : undefined
        const withoutPreviousPulse = previous?.pin.startsWith('q')
          ? { ...block, enables: (block.enables ?? []).filter((path) => path.root !== `${previous.block}:eno` || path.steps?.length) }
          : block
        const connected = connect(withoutPreviousPulse, row.target, sourcePath(pending.from))
        // A value from another block carries its producer's execution pulse
        // too. A tree variable has no block ENO, so it never gets this link.
        return row.target.kind === 'in' && pending.from.pin.startsWith('q')
          ? connect(connected, { block: row.target.block, kind: 'en' }, sourcePath({ block: pending.from.block, pin: 'eno' }))
          : connected
      })
      return
    }
    // A chip moved from another pin leaves it; an empty chip ({ root: '' }) is chosen in the block details.
    const from = pending.moveFrom
    if (from && !(from.block === row.target.block && from.kind === row.target.kind && ('index' in from ? from.index : -1) === ('index' in row.target ? row.target.index : -1))) {
      workspace.updateBlock(from.block, (block) => disconnect(block, from))
    }
    workspace.updateBlock(row.target.block, (block) => connect(block, row.target, pending.path ?? { root: '' }))
    if (!pending.path) workspace.selectBlock(row.target.block)
    // A free chip put back on a pin goes: its pins show it docked again.
    if (pending.fromFree) {
      const id = pending.fromFree
      workspace.updateActive((canvas) => ({ ...canvas, variables: (canvas.variables ?? []).filter((entry) => entry.id !== id) }))
      setSelectedChip(undefined)
    }
    onClearSelectedObject?.()
  }
  const kindOfPath = (path: ObjectPath) => (path.steps?.length ? 'any' as const : variableKind(project ? findObject(project, path.root)?.node : undefined))
  const blocksById = new Map((active?.blocks ?? []).map((block) => [block.id, block]))
  /** The chip a pin shows: none for a wire, or where a free label represents this exact pin. */
  const chipOf = (path: ObjectPath, output: boolean, target?: WireTarget) => {
    if (!output && (sourceOf(path, blocksById) || (target && active && chipAtTarget(active, path, target)))) return undefined
    const full = pathLabel(path, project, active?.blocks)
    return { label: chipLabel(full), full }
  }
  const pressChip = (target: WireTarget, path: ObjectPath, event: React.PointerEvent) => {
    surfaceRef.current?.setPointerCapture(event.pointerId)
    setChipPress({ kind: 'docked', target, path, start: { x: event.clientX, y: event.clientY } })
  }
  const movedChip = (clientX: number, clientY: number) => {
    const press = chipPress!
    if (press.kind === 'free') {
      const raw = { x: press.origin.x + (clientX - press.start.x) / viewportRef.current.zoom, y: press.origin.y + (clientY - press.start.y) / viewportRef.current.zoom }
      setChipPress({ ...press, at: snap ? snapPoint(raw) : raw })
      // Over a block: the picker opens, a pin there takes the chip back.
      const variable = active?.variables?.find((entry) => entry.id === press.id)
      const point = screenToCanvas(viewportRef.current, local(clientX, clientY))
      if (variable && (blockAt(point) || rowAt(clientX, clientY))) track({ kind: 'variable', path: variable.path, carries: kindOfPath(variable.path), fromFree: variable.id }, clientX, clientY)
      else if (draft) setDraft(undefined)
      return
    }
    if (!draft && Math.hypot(clientX - press.start.x, clientY - press.start.y) < CLICK_SLOP) return
    // Pulled off: it is now a variable on the move (onto another pin, or onto the canvas).
    if (!draft) setDraft({ pending: { kind: 'variable', path: press.path, carries: kindOfPath(press.path), moveFrom: press.target.kind === 'out' ? undefined : press.target }, at: { x: 0, y: 0 }, ghost: chipLabel(pathLabel(press.path, project, active?.blocks)) })
    track({ kind: 'variable', path: press.path, carries: kindOfPath(press.path), moveFrom: press.target.kind === 'out' ? undefined : press.target }, clientX, clientY)
  }
  const releasedChip = (clientX: number, clientY: number) => {
    const press = chipPress!
    setChipPress(undefined)
    if (press.kind === 'free') {
      // Onto a block: a pin takes it back (docked again), or the picker stays open to choose one.
      if (draft && (draft.over || draft.hovered)) return finish(clientX, clientY)
      if (press.at.x !== press.origin.x || press.at.y !== press.origin.y) workspace.updateActive((canvas) => ({ ...canvas, variables: (canvas.variables ?? []).map((entry) => (entry.id === press.id ? { ...entry, x: press.at.x, y: press.at.y } : entry)) }))
      else {
        setSelectedChip(press.id)
        setSelectedWire(undefined)
        workspace.selectBlock(undefined)
      }
      return
    }
    if (!draft) {
      // A click: the chip is selected (Delete takes it off) and the block's details open to edit it.
      workspace.selectBlock(press.target.block)
      setSelectedPinChip(press.target)
      setSelectedWire(undefined)
      setSelectedChip(undefined)
      return
    }
    setSelectedPinChip(undefined)
    if (draft.over || draft.hovered) return finish(clientX, clientY)
    // Dropped on the empty canvas: this label represents only the pin it came from.
    if (!draft.over && !draft.hovered && press.target.kind !== 'out') {
      const at = snap ? snapPoint({ x: draft.at.x - CHIP_WIDTH / 2, y: draft.at.y - CHIP_HEIGHT / 2 }) : { x: draft.at.x - CHIP_WIDTH / 2, y: draft.at.y - CHIP_HEIGHT / 2 }
      const target = press.target.kind === 'in'
        ? { block: press.target.block, kind: 'in' as const, index: press.target.index }
        : { block: press.target.block, kind: 'en' as const, ...('index' in press.target && press.target.index !== undefined ? { index: press.target.index } : {}) }
      workspace.updateActive((canvas) => {
        const variables = canvas.variables ?? []
        const prefix = `var-${Date.now().toString(36)}`
        let id = prefix
        let suffix = 1
        while (variables.some((entry) => entry.id === id)) id = `${prefix}-${suffix++}`
        return { ...canvas, variables: [...variables, { id, path: press.path, targets: [target], ...at }] }
      })
      setDraft(undefined)
    }
  }
  const startChipWire = (id: string, event: React.PointerEvent) => {
    const variable = active?.variables?.find((entry) => entry.id === id)
    if (!variable) return
    surfaceRef.current?.setPointerCapture(event.pointerId)
    setSelectedChip(undefined)
    setDraft({ pending: { kind: 'variable', path: variable.path, carries: kindOfPath(variable.path) }, at: { x: variable.x + CHIP_WIDTH, y: variable.y + CHIP_HEIGHT / 2 }, from: { x: variable.x + CHIP_WIDTH, y: variable.y + CHIP_HEIGHT / 2 } })
  }
  /** Released: on a picker row it connects; on the block it keeps the picker open to choose; elsewhere it drops. */
  const finish = (clientX: number, clientY: number, pending = draft?.pending) => {
    if (!pending) return
    const key = rowAt(clientX, clientY)
    const blockId = draft?.over
    const row = rowsFor(blockId, pending).find((entry) => entry.key === key)
    if (row) pick(row, pending)
    else if (blockId) setDraft({ pending, at: draft!.at, over: blockId, picking: true })
    else setDraft(undefined)
  }
  const startWire = (from: WireSource, event: React.PointerEvent) => {
    const block = active?.blocks.find((entry) => entry.id === from.block)
    if (!block) return
    surfaceRef.current?.setPointerCapture(event.pointerId)
    const output = from.pin.startsWith('q') ? shapeOf(block).outputs[Number(from.pin.slice(1))]?.value : undefined
    setSelectedWire(undefined)
    track({ kind: 'wire', from, carries: sourceKind(from.pin, output), ...(from.pin === 'eno' && block.type === 'FOR' ? { loop: true } : {}) }, event.clientX, event.clientY)
  }
  const onBlockSelect = (block: CanvasBlock, additive = false) => {
    if (selectedObjectNode && project) {
      const targetId = selectedObjectNode.kind === 'reference' ? selectedObjectNode.targetId : selectedObjectNode.id
      const targetNode = selectedObjectNode.kind === 'reference' ? findObject(project, targetId)?.node : selectedObjectNode
      const carries = variableKind(targetNode)
      const shape = shapeOf(block)
      setDraft({
        pending: { kind: 'variable', path: { root: selectedObjectNode.id }, carries },
        at: { x: block.x + shape.width / 2, y: block.y + shape.height / 2 },
        over: block.id,
        picking: true,
      })
      return
    }
    // Several blocks: a tap adds the block or takes it out; a press on one of a selected group keeps the group (to drag it).
    tapAction.current = undefined
    if (additive || workspace.multiSelect) {
      if (selectedIds.includes(block.id)) tapAction.current = { id: block.id, kind: 'toggle' }
      else workspace.toggleBlock(block.id)
      return
    }
    if (selectedIds.length > 1 && selectedIds.includes(block.id)) {
      tapAction.current = { id: block.id, kind: 'only' }
      return
    }
    // A block is selected and another is touched: two windows, what the selected one sends and where the touched one takes it.
    if (selectedBlock && selectedBlock.id !== block.id) {
      const forward = fittingSources(selectedBlock, block).length > 0
      if (forward || fittingSources(block, selectedBlock).length > 0) {
        const size = shapeOf(block)
        const [fromBlock, toBlock] = forward ? [selectedBlock, block] : [block, selectedBlock]
        const first = firstSource(fittingSources(fromBlock, toBlock))
        setDraft({ pending: fittingSources(fromBlock, toBlock)[first]!.pending, pair: { from: fromBlock.id, to: toBlock.id, source: first }, at: { x: block.x + size.width / 2, y: block.y + size.height / 2 }, over: block.id, picking: true })
        return
      }
    }
    workspace.selectBlock(block.id)
  }
  const draggingObject = (event: React.DragEvent) => !!active && event.dataTransfer.types.includes(OBJECT_DRAG_TYPE)
  const pickerBlock = draft?.over ? active?.blocks.find((entry) => entry.id === draft.over) : undefined

  // A block dragged from the palette: where its corner would land.
  const dropPoint = (event: React.DragEvent): Point => {
    const under = screenToCanvas(viewport, local(event.clientX, event.clientY))
    const corner = { x: under.x - GRAB.x, y: under.y - GRAB.y }
    return snap ? snapPoint(corner) : corner
  }
  const fromPalette = (event: React.DragEvent) => !!active && event.dataTransfer.types.includes(BLOCK_DRAG_TYPE)
  const ghostShape = ghost && blockShape(ghost.type ? catalog.block(ghost.type) : undefined, { id: '', type: ghost.type ?? '' }, workspace.detailed)

  return (
    <div
      ref={surfaceRef}
      className={`canvas-surface ${panning ? 'is-panning' : ''}`}
      style={gridStyle}
      aria-label={active ? `Canvas ${active.name}` : 'No canvas'}
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerEnd}
      onPointerCancel={onPointerEnd}
      onPointerLeave={() => setCursor(undefined)}
      onDragOver={(event) => {
        if (draggingObject(event)) {
          event.preventDefault()
          event.dataTransfer.dropEffect = 'link'
          track({ kind: 'variable', carries: kindOfDrag([...event.dataTransfer.types]) ?? 'any' }, event.clientX, event.clientY)
          return
        }
        if (!fromPalette(event)) return
        event.preventDefault()
        event.dataTransfer.dropEffect = 'copy'
        setGhost({ at: dropPoint(event), type: workspace.paletteDrag.current })
      }}
      onDragLeave={(event) => {
        if (event.currentTarget.contains(event.relatedTarget as Node | null)) return
        setGhost(undefined)
        if (draft && !draft.picking && draft.pending.kind === 'variable') setDraft(undefined)
      }}
      onDrop={(event) => {
        setGhost(undefined)
        if (draggingObject(event) && draft) {
          event.preventDefault()
          const id = event.dataTransfer.getData(OBJECT_DRAG_TYPE)
          finish(event.clientX, event.clientY, { kind: 'variable', ...(id ? { path: { root: id } } : {}), carries: draft.pending.carries })
          return
        }
        if (!fromPalette(event)) return
        event.preventDefault()
        const key = event.dataTransfer.getData(BLOCK_DRAG_TYPE)
        if (key) workspace.placeBlock(key, dropPoint(event))
      }}
    >
      <div className="canvas-world" style={{ transform: `translate(${viewport.x}px, ${viewport.y}px) scale(${viewport.zoom})` }}>
        <div className="canvas-origin" aria-hidden="true" />
        {active && <CanvasGroups canvas={active} catalog={catalog} shapeOf={shapeOf} />}
        {active && <CanvasWires canvas={active} shapeOf={shapeOf} selected={selectedWire} draft={draft && !draft.picking && (draft.pending.kind === 'wire' || draft.from) ? { from: draft.pending.kind === 'wire' ? draft.pending.from : draft.from!, to: draft.at } : undefined} onSelect={(wire) => { setSelectedWire(wire); setSelectedChip(undefined); setSelectedPinChip(undefined); workspace.selectBlock(undefined) }} />}
        {active?.variables?.map((variable) => {
          const moving = chipPress?.kind === 'free' && chipPress.id === variable.id ? chipPress.at : undefined
          const full = pathLabel(variable.path, project, active?.blocks)
          return <FreeChip key={variable.id} variable={moving ? { ...variable, ...moving } : variable} label={chipLabel(full)} full={full} selected={selectedChip === variable.id} onPress={(event) => {
            surfaceRef.current?.setPointerCapture(event.pointerId)
            setChipPress({ kind: 'free', id: variable.id, start: { x: event.clientX, y: event.clientY }, origin: { x: variable.x, y: variable.y }, at: { x: variable.x, y: variable.y } })
          }} onWireStart={(event) => startChipWire(variable.id, event)} />
        })}
        {draft?.ghost && !draft.picking && !draft.over && <div className="canvas-chip is-ghost" aria-hidden="true" style={{ left: draft.at.x - CHIP_WIDTH / 2, top: draft.at.y - CHIP_HEIGHT / 2, width: CHIP_WIDTH, height: CHIP_HEIGHT }}>{draft.ghost}</div>}
        {active?.blocks.map((block) => {
          const type = catalog.block(block.type)
          return (
            <CanvasBlockView
              key={block.id}
              block={block}
              type={type}
              shape={blockShape(type, block, workspace.detailed)}
              detailed={workspace.detailed}
              devices={devices} deviceCatalog={deviceCatalog}
              selected={selectedIds.includes(block.id)}
              errors={(diagnostics.get(block.id) ?? []).filter((entry) => entry.severity === 'error').length}
              zoom={viewport.zoom}
              snap={snap}
              isLinking={isLinking}
              onSelect={(additive) => onBlockSelect(block, additive)}
              onTap={() => {
                const action = tapAction.current
                tapAction.current = undefined
                if (action?.id !== block.id) return
                if (action.kind === 'toggle') workspace.toggleBlock(block.id)
                else workspace.selectBlock(block.id)
              }}
              onMove={(to) => {
                tapAction.current = undefined
                // A block of a selected group takes the whole group along, as one undo step.
                if (selectedIds.length > 1 && selectedIds.includes(block.id)) workspace.moveBlocks(selectedIds, { x: to.x - block.x, y: to.y - block.y })
                else workspace.moveBlock(block.id, to)
              }}
              onDrag={(by) => setGroupDrag(by && selectedIds.length > 1 && selectedIds.includes(block.id) ? { from: block.id, by } : undefined)}
              offset={groupDrag && groupDrag.from !== block.id && selectedIds.includes(block.id) ? groupDrag.by : undefined}
              onRename={(name) => workspace.updateBlock(block.id, (current) => ({ ...current, name }))}
              onWireStart={startWire}
              chipOf={chipOf}
              onChipPress={pressChip}
              selectedChip={selectedPinChip?.block === block.id ? selectedPinChip : undefined}
              labelOf={(path) => pathLabel(path, project, active?.blocks)}
            />
          )
        })}
        {ghost && ghostShape && <div className="canvas-block-ghost" aria-hidden="true" style={{ left: ghost.at.x, top: ghost.at.y, width: ghostShape.width, height: ghostShape.height }} />}
      </div>
      {draft?.pair && (() => {
        const from = active?.blocks.find((entry) => entry.id === draft.pair!.from)
        const to = active?.blocks.find((entry) => entry.id === draft.pair!.to)
        if (!from || !to) return null
        const sources = fittingSources(from, to)
        const chosen = Math.min(draft.pair.source, Math.max(0, sources.length - 1))
        const pending = sources[chosen]?.pending
        return (
          <LinkWindows
            from={from}
            fromShape={shapeOf(from)}
            to={to}
            toShape={shapeOf(to)}
            sources={sources.map((source) => source.choice)}
            chosen={chosen}
            rows={pending ? rowsFor(to.id, pending).filter((row) => row.ok) : []}
            viewport={viewport}
            canSwap={fittingSources(to, from).length > 0}
            onChoose={(index) => setDraft((current) => (current?.pair ? { ...current, pending: sources[index]!.pending, pair: { ...current.pair, source: index } } : current))}
            onPick={(row) => { if (pending) pick(row, pending) }}
            onSwap={() => setDraft((current) => {
              if (!current?.pair) return current
              const back = fittingSources(to, from)
              const first = firstSource(back)
              return { ...current, pending: back[first]!.pending, pair: { from: to.id, to: from.id, source: first } }
            })}
            onCancel={() => setDraft(undefined)}
          />
        )
      })()}
      {draft && !draft.pair && pickerBlock && (
        <WirePicker
          block={pickerBlock}
          shape={shapeOf(pickerBlock)}
          rows={rowsFor(pickerBlock.id, draft.pending)}
          viewport={viewport}
          hovered={draft.hovered}
          picking={!!draft.picking}
          onPick={(row) => pick(row, draft.pending)}
          onCancel={() => {
            setDraft(undefined)
            onClearSelectedObject?.()
          }}
        />
      )}
      {marquee && <div className="canvas-marquee" aria-hidden="true" style={{ left: Math.min(marquee.from.x, marquee.to.x), top: Math.min(marquee.from.y, marquee.to.y), width: Math.abs(marquee.to.x - marquee.from.x), height: Math.abs(marquee.to.y - marquee.from.y) }} />}
      {workspace.multiSelect && !selectedWire && !selectedChip && !selectedPinChip && !draft && !selectedObjectNode && <div className="canvas-wire-hint">Select multiple: tap blocks to add or remove them, drag the background to draw a box · {selectedIds.length} selected</div>}
      {selectedWire && <div className="canvas-wire-hint">Wire selected: Delete removes it, Esc keeps it</div>}
      {selectedChip && <div className="canvas-wire-hint">Variable chip selected: Delete puts it back on its pins, Esc keeps it</div>}
      {selectedPinChip && <div className="canvas-wire-hint">Variable selected: Delete takes it off the pin, drag it to move it, Esc keeps it</div>}
      {selectedObjectNode && !draft?.picking && (
        <div className="canvas-wire-hint">
          Variable &quot;{selectedObjectNode.name || 'selected'}&quot;: click a block to choose a pin · Esc to cancel
        </div>
      )}
      {draft?.picking && (
        <div className="canvas-wire-hint">
          Choose a pin for &quot;{selectedObjectNode?.name ?? (draft.pending.kind === 'variable' && draft.pending.path ? pathLabel(draft.pending.path, project, active?.blocks) : 'variable')}&quot; · Esc to cancel
        </div>
      )}
      {marker && snap && !panning && !ghost && <div className="canvas-snap-marker" aria-hidden="true" style={{ left: marker.x, top: marker.y }} />}
      {!active && (
        <div className="canvas-empty">
          <p>No canvas yet.</p>
          <Button onClick={workspace.create}><Plus aria-hidden="true" />New canvas</Button>
        </div>
      )}
      {empty && !ghost && <div className="canvas-hint" aria-hidden="true">Drag blocks here from the palette · drag the background to move, scroll or pinch to zoom</div>}
      {workspace.error && <button type="button" className="canvas-error" onClick={workspace.clearError} title="Dismiss">{workspace.error}</button>}
      {debug.error && <button type="button" className="canvas-error" style={workspace.error ? { top: 52 } : undefined} onClick={debug.clearError} title="Dismiss">Debug: {debug.error}</button>}
      {debug.active && <div className="canvas-wire-hint">{debug.stale ? 'Debug: the program changed since the upload, use Upload again in the Debug panel' : 'Debug mode: strips and tint show what the board runs, values sit beside the pins'}</div>}
      <div className="canvas-status" aria-live="off">
        {snapped ? `x ${Math.round(snapped.x)}  y ${Math.round(snapped.y)}` : ''}
        {snap ? <span className="canvas-status-snap">snap {GRID}</span> : null}
      </div>
      <div className="canvas-zoom" role="toolbar" aria-label="Zoom">
        <button type="button" aria-label="Zoom out" title="Zoom out" onClick={() => zoomButton(1 / BUTTON_ZOOM)}><Minus aria-hidden="true" /></button>
        <button type="button" className="canvas-zoom-value" title="Zoom" onClick={() => zoomButton(1 / viewport.zoom)}>{`${Math.round(viewport.zoom * 100)}%`}</button>
        <button type="button" aria-label="Zoom in" title="Zoom in" onClick={() => zoomButton(BUTTON_ZOOM)}><Plus aria-hidden="true" /></button>
        <button type="button" aria-label="Back to the origin" title="Back to the origin at 100 %" onClick={recenter}><Crosshair aria-hidden="true" /></button>
      </div>
    </div>
  )
}
