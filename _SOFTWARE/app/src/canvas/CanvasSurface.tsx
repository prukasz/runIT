import { useEffect, useLayoutEffect, useRef, useState } from 'react'
import type { CSSProperties } from 'react'
import { Crosshair, Minus, Plus } from 'lucide-react'
import { Button } from '../components/Button'
import { canvasToScreen, CHIP_HEIGHT, CHIP_WIDTH, chipLabel, connect, disconnect, freeChipFor, GRID, pathLabel, screenToCanvas, snapPoint, sourcePath, zoomAt } from '../domain/canvas'
import type { CanvasBlock, ObjectPath, Point, Viewport, Wire, WireSource, WireTarget } from '../domain/canvas'
import { OBJECT_DRAG_TYPE } from '../domain/objects/drag'
import { findObject } from '../domain/project'
import type { ProjectDocument } from '../domain/project'
import { BLOCK_DRAG_TYPE } from '../blocks/BlockPalette'
import { blockShape, decodePlacement, descriptorOf } from '../blocks/blockModel'
import { CanvasBlockView } from './CanvasBlockView'
import { CanvasWires } from './CanvasWires'
import { FreeChip } from './FreeChip'
import { LinkWindows } from './LinkWindows'
import type { PickerRow, SourceChoice } from './LinkWindows'
import type { CanvasWorkspace } from './useCanvasWorkspace'

/*
 * The open canvas: a grid that pans (drag the background, one finger) and zooms (wheel, pinch, the buttons) around the pointer, and
 * its blocks in the world layer, which carries the viewport transform. Blocks dropped from the palette show where they will land
 * first. A click on the background clears the selection; Delete removes the selected blocks or wire. Several blocks are selected
 * with Shift / Ctrl, or by drawing a box on the background with Shift; a dragged selected block takes the others along. A wire is
 * dragged from an output (or ENO) onto a block, which asks for the pin.
 */

/** Every MAJOR-th grid line is drawn stronger. */
const MAJOR = 5
const WHEEL_ZOOM = 0.0015
const BUTTON_ZOOM = 1.25
/** Where a dropped block's corner sits relative to the pointer: the pointer holds its header. */
const GRAB = { x: 24, y: 14 }
/** A press that moves less than this is a click. */
const CLICK_SLOP = 4

/** What a pin carries, for deciding what fits where: numbers and bools cast to each other, whole objects don't. */
const kindOfPin = (value: string): 'number' | 'bool' | 'object' => (value === 'gate' || value === 'bool' ? 'bool' : value === 'object' || value === 'ptr-cell' ? 'object' : 'number')

/** Whether what is sent fits a target: the EN strip (`en`) or an input pin of that value kind. */
const fits = (target: string, sent: 'number' | 'bool' | 'object'): boolean => (target === 'en' ? sent !== 'object' : target === 'object' ? true : target === 'ptr-cell' ? false : sent !== 'object')

const isEditing = (target: EventTarget | null): boolean => target instanceof HTMLElement && (target.isContentEditable || !!target.closest('input, textarea, select, [contenteditable="true"]'))

export function CanvasSurface({ workspace, showGrid, project, onBackgroundClick }: { workspace: CanvasWorkspace; showGrid: boolean; project?: ProjectDocument; onBackgroundClick?: () => void }) {
  const { active, selectedIds } = workspace
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
  // A wire being dragged from a pin (the pointer in canvas units), and the block it was dropped on (asking for the pin).
  const [draft, setDraft] = useState<{ from: WireSource; at: Point }>()
  const [picker, setPicker] = useState<{ path: ObjectPath; block: string; label: string; kind?: 'bool' | 'other'; moveFrom?: WireTarget }>()
  const [selectedWire, setSelectedWire] = useState<Wire>()
  // A press on a variable chip docked on a pin: a click selects it, a drag pulls it off to move it to another pin.
  const [chipPress, setChipPress] = useState<{ target: WireTarget; path: ObjectPath; start: Point; pulled: boolean }>()
  const [chipGhost, setChipGhost] = useState<{ at: Point; label: string }>()
  const [selectedChip, setSelectedChip] = useState<WireTarget>()
  // A free chip (a variable pulled off its pin): the selected one, and a wire being dragged from one (the pointer in canvas units).
  const [selectedFree, setSelectedFree] = useState<string>()
  const [chipDraft, setChipDraft] = useState<{ id: string; at: Point }>()
  // Two blocks linked by clicking: the one selected first is the source, the one clicked next the target.
  const [link, setLink] = useState<{ from: string; to: string; source: number }>()
  // A box being drawn on the background (screen points), and how far the dragged block of a selected group has moved.
  const [marquee, setMarquee] = useState<{ from: Point; to: Point }>()
  const [groupDrag, setGroupDrag] = useState<{ from: string; by: Point }>()
  // A press on a selected block waits to see whether it becomes a drag (the group moves) or a tap.
  const tapAction = useRef<{ id: string; kind: 'toggle' | 'only' | 'link'; from?: string } | undefined>(undefined)

  /** The name of a block's output pin, for `pathLabel` (the pins do not depend on the labels). */
  const outputTitle = (block: CanvasBlock, index: number) => blockShape(descriptorOf(block.type), block).outputs.find((pin) => pin.index === index)?.title
  const labelOf = (path: ObjectPath) => pathLabel(path, project, active?.blocks, outputTitle)
  const shapeOf = (block: CanvasBlock) => blockShape(descriptorOf(block.type), block, labelOf)
  /** A variable of the tree, as opposed to another block's pin. */
  const isVariable = (path: ObjectPath) => !/^.+:(q\d+|eno|body)$/.test(path.root)

  const save = (delay = 0) => {
    window.clearTimeout(saveTimer.current)
    saveTimer.current = window.setTimeout(() => workspace.setViewport(viewportRef.current), delay)
  }

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

  // The middle of the view, for blocks placed by a click in the palette.
  useEffect(() => {
    const surface = surfaceRef.current
    if (!surface || typeof ResizeObserver === 'undefined') return
    const observer = new ResizeObserver(() => { workspace.surfaceSize.current = { width: surface.clientWidth, height: surface.clientHeight } })
    observer.observe(surface)
    return () => observer.disconnect()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const removeSelectedWire = (): boolean => {
    if (selectedFree) {
      workspace.removeFreeChip(selectedFree)
      setSelectedFree(undefined)
      return true
    }
    if (selectedChip) {
      workspace.updateBlock(selectedChip.block, (block) => disconnect(block, selectedChip))
      setSelectedChip(undefined)
      return true
    }
    if (!selectedWire) return false
    workspace.updateBlock(selectedWire.to.block, (block) => disconnect(block, selectedWire.to, selectedWire.enableIndex))
    setSelectedWire(undefined)
    return true
  }

  // Delete removes the selected wire or blocks, Escape clears the selection and any wire in progress (not while typing in a field).
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (isEditing(event.target)) return
      if (event.key === 'Escape') {
        setDraft(undefined)
        setPicker(undefined)
        setLink(undefined)
        setSelectedWire(undefined)
        setSelectedChip(undefined)
        setSelectedFree(undefined)
        setChipDraft(undefined)
        workspace.selectBlock(undefined)
      } else if (event.key === 'Delete' || event.key === 'Backspace') {
        if (event.ctrlKey || event.metaKey || event.altKey) return
        if (removeSelectedWire()) event.preventDefault()
        else if (selectedIds.length) {
          event.preventDefault()
          workspace.deleteBlocks(selectedIds)
        }
      } else if ((event.ctrlKey || event.metaKey) && !event.altKey && !event.shiftKey) {
        const key = event.key.toLowerCase()
        if (key === 'c' && selectedIds.length && !window.getSelection()?.toString()) {
          event.preventDefault()
          workspace.copy()
        } else if (key === 'v' && workspace.canPaste) {
          event.preventDefault()
          workspace.paste()
        }
      }
    }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  })

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

  const blockAt = (point: Point, except?: string): CanvasBlock | undefined =>
    [...(active?.blocks ?? [])].reverse().find((block) => {
      if (block.id === except) return false
      const shape = shapeOf(block)
      return point.x >= block.x - 12 && point.x <= block.x + shape.width + 12 && point.y >= block.y && point.y <= block.y + shape.height
    })

  /** The pin (an input, an output or the EN strip) under a screen point, found by the data attributes the block view puts on them. */
  const pinAtPoint = (clientX: number, clientY: number, except?: string): { block: string; target: WireTarget } | undefined => {
    for (const element of document.elementsFromPoint(clientX, clientY)) {
      const pin = element instanceof HTMLElement ? element.closest<HTMLElement>('[data-pin-kind]') : null
      const block = pin?.dataset.block
      if (!pin || !block || block === except) continue
      const kind = pin.dataset.pinKind
      if (kind === 'en') return { block, target: { block, kind: 'en' } }
      if (kind === 'in' || kind === 'out') return { block, target: { block, kind, index: Number(pin.dataset.pinIndex) } }
    }
    return undefined
  }

  /** What a variable sends: a bool or a number (they cast to each other), or a whole object. */
  const variableKind = (path: ObjectPath): 'number' | 'bool' | 'object' => {
    if (path.steps?.length) return 'number'
    const node = project && findObject(project, path.root)?.node
    if (!node) return 'number'
    return node.kind === 'value' ? (node.type === 'B' ? 'bool' : 'number') : 'object'
  }

  /** Whether a variable can go on this pin: EN takes a bool or number, an input its value kind, an output a single value. */
  const variableFits = (blockId: string, target: WireTarget, path: ObjectPath): boolean => {
    const block = active?.blocks.find((entry) => entry.id === blockId)
    if (!block) return false
    const shape = shapeOf(block)
    const sent = variableKind(path)
    if (target.kind === 'en') return sent !== 'object'
    if (target.kind === 'in') {
      const pin = shape.inputs.find((entry) => entry.index === target.index)
      return !!pin && (pin.value === 'ptr-cell' || fits(pin.value, sent))
    }
    return shape.outputs.some((entry) => entry.index === target.index) && sent !== 'object'
  }

  /**
   * A variable let go at a screen point. On a pin that takes it, it connects there with no menu (a docked chip leaves its old pin,
   * a free chip docks and goes); on a block but not on a pin, the pins are offered (unless `ask` is false); false = open canvas.
   */
  const dropVariable = (path: ObjectPath, clientX: number, clientY: number, options: { moveFrom?: WireTarget; fromFree?: string; ask?: boolean } = {}): boolean => {
    if (!active) return false
    const hit = pinAtPoint(clientX, clientY)
    if (hit && variableFits(hit.block, hit.target, path)) {
      const { moveFrom, fromFree } = options
      if (fromFree) workspace.dockFreeChip(fromFree, hit.block, hit.target)
      else {
        const stays = moveFrom && moveFrom.block === hit.block && moveFrom.kind === hit.target.kind && ('index' in moveFrom ? moveFrom.index : -1) === ('index' in hit.target ? hit.target.index : -1)
        if (moveFrom && !stays) workspace.updateBlock(moveFrom.block, (block) => disconnect(block, moveFrom))
        workspace.updateBlock(hit.block, (block) => connect(block, hit.target, path))
      }
      return true
    }
    if (options.ask === false) return false
    const over = blockAt(screenToCanvas(viewportRef.current, local(clientX, clientY)))
    if (!over) return false
    setPicker({ path, block: over.id, label: labelOf(path), kind: variableKind(path) === 'bool' ? 'bool' : 'other', ...(options.moveFrom ? { moveFrom: options.moveFrom } : {}) })
    return true
  }

  /** Where a free chip lands when it is let go on the open canvas: the pointer holds its middle. */
  const chipSpot = (clientX: number, clientY: number): Point => {
    const at = screenToCanvas(viewportRef.current, local(clientX, clientY))
    return snapPoint({ x: at.x - CHIP_WIDTH / 2, y: at.y - CHIP_HEIGHT / 2 })
  }

  const onPointerDown = (event: React.PointerEvent<HTMLDivElement>) => {
    if (event.button !== 0 && event.button !== 1) return
    // Blocks, the zoom buttons, the picker and messages handle their own presses.
    if ((event.target as HTMLElement).closest('.canvas-block, .canvas-zoom, .canvas-error, .canvas-empty, .wire-picker')) return
    setDraft(undefined)
    setPicker(undefined)
    setLink(undefined)
    setSelectedWire(undefined)
    setSelectedChip(undefined)
    setSelectedFree(undefined)
    event.currentTarget.setPointerCapture(event.pointerId)
    const point = local(event.clientX, event.clientY)
    pointers.current.set(event.pointerId, point)
    pressAt.current = pointers.current.size === 1 ? point : undefined
    // Shift: one finger draws a box instead of panning; a second finger still pinches.
    if (pointers.current.size === 1 && event.shiftKey) setMarquee({ from: point, to: point })
    else setPanning(true)
  }

  const onPointerMove = (event: React.PointerEvent<HTMLDivElement>) => {
    const point = local(event.clientX, event.clientY)
    if (event.pointerType === 'mouse') setCursor(point)
    if (chipPress) {
      if (chipPress.pulled || Math.hypot(event.clientX - chipPress.start.x, event.clientY - chipPress.start.y) >= CLICK_SLOP) {
        if (!chipPress.pulled) setChipPress({ ...chipPress, pulled: true })
        setChipGhost({ at: point, label: chipLabel(labelOf(chipPress.path)) })
      }
      return
    }
    if (chipDraft) {
      setChipDraft({ ...chipDraft, at: screenToCanvas(viewportRef.current, point) })
      return
    }
    if (draft) {
      setDraft({ ...draft, at: screenToCanvas(viewportRef.current, point) })
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

  const onPointerEnd = (event: React.PointerEvent<HTMLDivElement>) => {
    if (chipPress) {
      const press = chipPress
      setChipPress(undefined)
      setChipGhost(undefined)
      if (!press.pulled) {
        // A click: the chip is selected (Delete takes it off) and the block's details open.
        workspace.selectBlock(press.target.block)
        setSelectedChip(press.target)
        setSelectedWire(undefined)
        return
      }
      // Pulled onto a pin: it moves there. On a block: its pins are offered. On the open canvas: it comes off the pin as a free chip,
      // still wired to it.
      if (!dropVariable(press.path, event.clientX, event.clientY, { moveFrom: press.target })) workspace.addFreeChip(press.path, chipSpot(event.clientX, event.clientY))
      return
    }
    if (chipDraft) {
      const chip = active?.variables?.find((entry) => entry.id === chipDraft.id)
      setChipDraft(undefined)
      if (chip) dropVariable(chip.path, event.clientX, event.clientY)
      return
    }
    if (draft) {
      // The wire is let go: on a block it asks which pin, anywhere else it is dropped.
      const hit = pinAtPoint(event.clientX, event.clientY, draft.from.block)
      const sourceBlock = active?.blocks.find((entry) => entry.id === draft.from.block)
      const sentPin = sourceBlock && shapeOf(sourceBlock).outputs.find((entry) => 'q' + entry.index === draft.from.pin)
      const sent = draft.from.pin === 'eno' || !sentPin ? 'bool' : kindOfPin(sentPin.value)
      const hitBlock = hit && active?.blocks.find((entry) => entry.id === hit.block)
      const hitPin = hit && hitBlock && hit.target.kind === 'in' ? shapeOf(hitBlock).inputs.find((entry) => entry.index === (hit.target as { index: number }).index) : undefined
      const accepts = !!hit && (hit.target.kind === 'en' ? fits('en', sent) : !!hitPin && fits(hitPin.value, sent))
      if (hit && accepts) {
        workspace.updateBlock(hit.block, (block) => connect(block, hit.target, sourcePath(draft.from)))
        setDraft(undefined)
        return
      }
      const over = blockAt(screenToCanvas(viewportRef.current, local(event.clientX, event.clientY)), draft.from.block)
      if (over) setPicker({ path: sourcePath(draft.from), block: over.id, label: pathLabel(sourcePath(draft.from), project, active?.blocks, outputTitle), kind: draft.from.pin === 'eno' ? 'bool' : undefined })
      setDraft(undefined)
      return
    }
    const press = pressAt.current
    if (press && pointers.current.size === 1 && pointers.current.has(event.pointerId)) {
      const end = local(event.clientX, event.clientY)
      if (Math.hypot(end.x - press.x, end.y - press.y) < CLICK_SLOP) {
        workspace.selectBlock(undefined)
        onBackgroundClick?.()
      }
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

  const startWire = (source: WireSource, event: React.PointerEvent) => {
    surfaceRef.current?.setPointerCapture(event.pointerId)
    setSelectedWire(undefined)
    setPicker(undefined)
    setDraft({ from: source, at: screenToCanvas(viewportRef.current, local(event.clientX, event.clientY)) })
  }

  const pick = (target: WireTarget) => {
    if (!picker) return
    const from = picker.moveFrom
    const same = from && from.block === target.block && from.kind === target.kind && ('index' in from ? from.index : -1) === ('index' in target ? target.index : -1)
    if (from && !same) workspace.updateBlock(from.block, (block) => disconnect(block, from))
    workspace.updateBlock(target.block, (block) => connect(block, target, picker.path))
    setPicker(undefined)
  }

  /** Selecting a block: a plain press selects only it, Shift / Ctrl toggles it in the group. */
  const onBlockSelect = (block: CanvasBlock, additive: boolean) => {
    setSelectedWire(undefined)
    setSelectedChip(undefined)
    setLink(undefined)
    const inGroup = selectedIds.includes(block.id)
    // One block is selected and another is pressed: a tap links them (a drag just moves the pressed one).
    if (!additive && selectedIds.length === 1 && selectedIds[0] !== block.id) {
      tapAction.current = { id: block.id, kind: 'link', from: selectedIds[0] }
      return
    }
    if (additive) {
      workspace.toggleBlock(block.id)
      tapAction.current = undefined
    } else if (inGroup && selectedIds.length > 1) tapAction.current = { id: block.id, kind: 'only' }
    else workspace.selectBlock(block.id)
  }

  // A block dragged from the palette: where its corner would land.
  const dropPoint = (event: React.DragEvent): Point => {
    const under = screenToCanvas(viewport, local(event.clientX, event.clientY))
    return snapPoint({ x: under.x - GRAB.x, y: under.y - GRAB.y })
  }
  const fromPalette = (event: React.DragEvent) => !!active && event.dataTransfer.types.includes(BLOCK_DRAG_TYPE)
  const ghostPlacement = ghost?.type ? decodePlacement(ghost.type) : undefined
  const ghostShape = ghost && blockShape(ghostPlacement ? descriptorOf(ghostPlacement.typeKey) : undefined, { id: '', type: ghostPlacement?.typeKey ?? '', ...(ghostPlacement?.settings ? { settings: ghostPlacement.settings } : {}) })

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
  const snapped = pointed && snapPoint(pointed)
  const marker = snapped && canvasToScreen(viewport, snapped)
  const empty = active && active.blocks.length === 0

  const linkFrom = link ? active?.blocks.find((entry) => entry.id === link.from) : undefined
  const linkTo = link ? active?.blocks.find((entry) => entry.id === link.to) : undefined
  const linkFromShape = linkFrom && shapeOf(linkFrom)
  const linkToShape = linkTo && shapeOf(linkTo)

  /** What a block sends: each output pin, and its ENO ("Done"). */
  const sourcesOf = (block: CanvasBlock, shape: ReturnType<typeof shapeOf>): SourceChoice[] => [
    ...shape.outputs.map((pin) => ({ key: `q${pin.index}`, label: pin.title, detail: block.outputs?.[pin.index] ? labelOf({ root: block.outputs[pin.index]! }) : 'Output of the block', type: pin.value })),
    { key: 'eno', label: 'Done', detail: 'true while the block acted', type: 'gate' },
  ]
  const linkSources = linkFrom && linkFromShape ? sourcesOf(linkFrom, linkFromShape) : []
  const chosen = link ? Math.min(link.source, Math.max(0, linkSources.length - 1)) : 0
  const sent = linkSources[chosen]
  const sentKind = sent ? kindOfPin(sent.type ?? 'number') : 'number'
  const linkRows: PickerRow[] = linkTo && linkToShape && sent ? [
    { key: 'en', label: 'Run when', detail: 'the block runs only while this is true', type: 'gate', target: { block: linkTo.id, kind: 'en' }, ok: fits('en', sentKind) },
    ...linkToShape.inputs.map((pin): PickerRow => {
      const current = linkTo.inputs?.[pin.index]
      return { key: `in${pin.index}`, label: pin.title, detail: current && current.root ? labelOf(current) : 'Unwired', type: pin.value, target: { block: linkTo.id, kind: 'in', index: pin.index }, ok: fits(pin.value, sentKind) }
    }),
  ] : []
  const linkInto = (row: PickerRow) => {
    if (!link || !sent) return
    workspace.updateBlock(row.target.block, (block) => connect(block, row.target, sourcePath({ block: link.from, pin: sent.key })))
    setLink(undefined)
  }

  const pickerBlock = picker ? active?.blocks.find((entry) => entry.id === picker.block) : undefined
  const pickerShape = pickerBlock && shapeOf(pickerBlock)
  const pickerAt = pickerBlock && pickerShape && canvasToScreen(viewport, { x: pickerBlock.x + pickerShape.width, y: pickerBlock.y })

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
        if (event.dataTransfer.types.includes(OBJECT_DRAG_TYPE) && active) {
          event.preventDefault()
          event.dataTransfer.dropEffect = 'link'
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
      }}
      onDrop={(event) => {
        setGhost(undefined)
        // A variable of the tree: dropped on a pin it connects there; on a block the pins are offered; on the open canvas it stays
        // there as a free chip.
        if (event.dataTransfer.types.includes(OBJECT_DRAG_TYPE) && active) {
          event.preventDefault()
          const id = event.dataTransfer.getData(OBJECT_DRAG_TYPE)
          if (id && !dropVariable({ root: id }, event.clientX, event.clientY)) workspace.addFreeChip({ root: id }, chipSpot(event.clientX, event.clientY))
          return
        }
        if (!fromPalette(event)) return
        event.preventDefault()
        const placement = decodePlacement(event.dataTransfer.getData(BLOCK_DRAG_TYPE))
        if (placement.typeKey) workspace.placeBlock(placement.typeKey, dropPoint(event), placement.settings)
      }}
    >
      <div className="canvas-world" style={{ transform: `translate(${viewport.x}px, ${viewport.y}px) scale(${viewport.zoom})` }}>
        <div className="canvas-origin" aria-hidden="true" />
        {active && (
          <CanvasWires
            canvas={active}
            shapeOf={shapeOf}
            selected={selectedWire}
            selectedPin={selectedChip}
            draft={draft && { from: draft.from, to: draft.at }}
            chipDraft={chipDraft && (() => {
              const chip = active.variables?.find((entry) => entry.id === chipDraft.id)
              return chip && { from: { x: chip.x + CHIP_WIDTH, y: chip.y + CHIP_HEIGHT / 2 }, to: chipDraft.at }
            })()}
            onSelect={(wire) => { setSelectedWire(wire); setSelectedChip(undefined); setSelectedFree(undefined); workspace.selectBlock(undefined) }}
            onSelectPin={(target) => { setSelectedChip(target); setSelectedWire(undefined); setSelectedFree(undefined); workspace.selectBlock(undefined) }}
          />
        )}
        {active?.blocks.map((block) => (
          <CanvasBlockView
            key={block.id}
            block={block}
            descriptor={descriptorOf(block.type)}
            shape={shapeOf(block)}
            selected={selectedIds.includes(block.id)}
            zoom={viewport.zoom}
            onSelect={(additive) => onBlockSelect(block, additive)}
            onTap={() => {
              const action = tapAction.current
              tapAction.current = undefined
              if (action?.id !== block.id) return
              if (action.kind === 'link' && action.from) setLink({ from: action.from, to: block.id, source: 0 })
              else if (action.kind === 'toggle') workspace.toggleBlock(block.id)
              else workspace.selectBlock(block.id)
            }}
            onMove={(to) => {
              // Dragging the pressed block (not tapping it): it becomes the selection.
              if (tapAction.current?.kind === 'link') workspace.selectBlock(block.id)
              tapAction.current = undefined
              // A block of a selected group takes the whole group along, as one undo step.
              if (selectedIds.length > 1 && selectedIds.includes(block.id)) workspace.moveBlocks(selectedIds, { x: to.x - block.x, y: to.y - block.y })
              else workspace.moveBlock(block.id, to)
            }}
            onDrag={(by) => setGroupDrag(by && selectedIds.length > 1 && selectedIds.includes(block.id) ? { from: block.id, by } : undefined)}
            offset={groupDrag && groupDrag.from !== block.id && selectedIds.includes(block.id) ? groupDrag.by : undefined}
            onRename={(name) => workspace.updateBlock(block.id, (current) => ({ ...current, name }))}
            onWireStart={startWire}
            labelOf={labelOf}
            isVariable={isVariable}
            isFree={(path) => !!freeChipFor(active!, path)}
            selectedChip={selectedChip?.block === block.id ? selectedChip : undefined}
            onChipPress={(target, path, event) => {
              surfaceRef.current?.setPointerCapture(event.pointerId)
              setPicker(undefined)
              setChipPress({ target, path, start: { x: event.clientX, y: event.clientY }, pulled: false })
            }}
          />
        ))}
        {active?.variables?.map((variable) => (
          <FreeChip
            key={variable.id}
            variable={variable}
            label={chipLabel(labelOf(variable.path))}
            full={labelOf(variable.path)}
            selected={selectedFree === variable.id}
            zoom={viewport.zoom}
            onSelect={() => { setSelectedFree(variable.id); setSelectedChip(undefined); setSelectedWire(undefined); workspace.selectBlock(undefined) }}
            onMove={(to) => workspace.moveFreeChip(variable.id, to)}
            onDropAt={(x, y) => dropVariable(variable.path, x, y, { fromFree: variable.id, ask: false })}
            onWireStart={(event) => {
              surfaceRef.current?.setPointerCapture(event.pointerId)
              setPicker(undefined)
              setChipDraft({ id: variable.id, at: screenToCanvas(viewportRef.current, local(event.clientX, event.clientY)) })
            }}
          />
        ))}
        {ghost && ghostShape && <div className="canvas-block-ghost" aria-hidden="true" style={{ left: ghost.at.x, top: ghost.at.y, width: ghostShape.width, height: ghostShape.height }} />}
      </div>
      {chipGhost && <div className="canvas-chip is-ghost" aria-hidden="true" style={{ left: chipGhost.at.x, top: chipGhost.at.y }}>{chipGhost.label}</div>}
      {picker && pickerBlock && pickerShape && pickerAt && (
        <div className="wire-picker" role="menu" aria-label={`Connect to ${pickerBlock.id}`} style={{ left: pickerAt.x + 8, top: pickerAt.y }}>
          <div className="wire-picker-head">{picker.label} → connect to</div>
          {picker.kind !== 'other' && <button type="button" role="menuitem" className="wire-picker-row" onClick={() => pick({ block: pickerBlock.id, kind: 'en' })}>Run when</button>}
          {pickerShape.inputs.map((pin) => (
            <button key={pin.index} type="button" role="menuitem" className="wire-picker-row" onClick={() => pick({ block: pickerBlock.id, kind: 'in', index: pin.index })}>{pin.title}</button>
          ))}
          {pickerShape.outputs.length > 0 && picker.path.root && isVariable(picker.path) && pickerShape.outputs.map((pin) => (
            <button key={`out${pin.index}`} type="button" role="menuitem" className="wire-picker-row" onClick={() => pick({ block: pickerBlock.id, kind: 'out', index: pin.index })}>{pin.title} (the block writes the variable)</button>
          ))}
          <button type="button" role="menuitem" className="wire-picker-row is-cancel" onClick={() => setPicker(undefined)}>Cancel</button>
        </div>
      )}
      {link && linkFrom && linkFromShape && linkTo && linkToShape && sent && (
        <LinkWindows
          from={linkFrom}
          fromShape={linkFromShape}
          to={linkTo}
          toShape={linkToShape}
          sources={linkSources}
          chosen={chosen}
          rows={linkRows}
          viewport={viewport}
          onChoose={(index) => setLink({ ...link, source: index })}
          onPick={linkInto}
          onSwap={() => setLink({ from: link.to, to: link.from, source: 0 })}
          onSelect={() => { workspace.selectBlock(link.to); setLink(undefined) }}
          onCancel={() => setLink(undefined)}
        />
      )}
      {marquee && <div className="canvas-marquee" aria-hidden="true" style={{ left: Math.min(marquee.from.x, marquee.to.x), top: Math.min(marquee.from.y, marquee.to.y), width: Math.abs(marquee.to.x - marquee.from.x), height: Math.abs(marquee.to.y - marquee.from.y) }} />}
      {selectedWire && <div className="canvas-wire-hint">Wire selected: Delete removes it, Esc keeps it</div>}
      {marker && !panning && !ghost && !draft && <div className="canvas-snap-marker" aria-hidden="true" style={{ left: marker.x, top: marker.y }} />}
      {!active && (
        <div className="canvas-empty">
          <p>No canvas yet.</p>
          <Button onClick={workspace.create}><Plus aria-hidden="true" />New canvas</Button>
        </div>
      )}
      {empty && !ghost && <div className="canvas-hint" aria-hidden="true">Drag blocks here from the palette · drag the background to move, scroll or pinch to zoom</div>}
      {workspace.error && <button type="button" className="canvas-error" onClick={workspace.clearError} title="Dismiss">{workspace.error}</button>}
      <div className="canvas-zoom" role="toolbar" aria-label="Zoom">
        <button type="button" aria-label="Zoom out" title="Zoom out" onClick={() => zoomButton(1 / BUTTON_ZOOM)}><Minus aria-hidden="true" /></button>
        <button type="button" className="canvas-zoom-value" title="Zoom" onClick={() => zoomButton(1 / viewport.zoom)}>{`${Math.round(viewport.zoom * 100)}%`}</button>
        <button type="button" aria-label="Zoom in" title="Zoom in" onClick={() => zoomButton(BUTTON_ZOOM)}><Plus aria-hidden="true" /></button>
        <button type="button" aria-label="Back to the origin" title="Back to the origin at 100 %" onClick={recenter}><Crosshair aria-hidden="true" /></button>
      </div>
    </div>
  )
}
