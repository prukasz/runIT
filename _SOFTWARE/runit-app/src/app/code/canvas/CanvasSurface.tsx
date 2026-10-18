import { useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react'
import type { CSSProperties } from 'react'
import { Crosshair, Minus, Plus, X } from 'lucide-react'
import { draftForVariable, newBlock, toPath } from '@engine'
import type { BlockInstance, ObjectPath, Source } from '@engine'
import { MenuOption, OptionMenu, TypeLead } from '@ui/components'
import { useErrors } from '../../console/useConsole'
import { blockDiagnostics, blockPinName, blockSource, chipLabel, connect, disconnect, fits, isBlockTarget, pinsOf, sameTarget, sentBy, sourceLabel } from '../blocks'
import type { BlockPin, PinTarget, Sent, Wire } from '../blocks'
import { BLOCK_DRAG_TYPE } from '../BlockPalette'
import type { BlocksState } from '../useBlocks'
import { decodePlacement } from '../useBlocks'
import { VARIABLE_DRAG_TYPE } from '../VariablesExplorer'
import { CanvasBlockView } from './CanvasBlockView'
import { pinLead } from '../PathEditor'
import { CanvasWires } from './CanvasWires'
import { FreeChip } from './FreeChip'
import { blockShape, canvasToScreen, CHIP_HEIGHT, CHIP_WIDTH, chipFor, GRID, positionOf, screenToCanvas, snapPoint, zoomAt } from './layout'
import type { BlockShape, Point, Viewport } from './layout'
import { LinkWindows } from './LinkWindows'
import type { PickerRow, SourceChoice } from './LinkWindows'
import './Canvas.css'

/*
 * The canvas of the Code view: a grid that pans (drag the background, one finger) and zooms (wheel, pinch, the buttons) around the
 * pointer, and the blocks in the world layer, which carries the viewport transform. A block dragged from the palette shows where it
 * will land. A click on the background clears the selection; Delete removes the selected blocks, wire or chip. Several blocks are
 * selected with Shift / Ctrl, or by drawing a box on the background with Shift; a dragged selected block takes the others along.
 *
 * Linking two blocks: select one, then tap the other; two windows open over them (what the first sends, where in the second it
 * goes). Or drag a wire from an output (or When done) onto a pin, or onto a block, which asks for the pin. A variable dropped from
 * the tree onto a pin goes on it; onto a block, the pins are offered; onto the open canvas, it stays there as a free chip.
 */

/** Every MAJOR-th grid point is drawn stronger. */
const MAJOR = 5
const WHEEL_ZOOM = 0.0015
const BUTTON_ZOOM = 1.25
/** Where a dropped block's corner sits from the pointer: the pointer holds its header. */
const GRAB = { x: 24, y: 14 }
/** A press that moves less than this is a click. */
const CLICK_SLOP = 4
/** How long after a tap a second click still makes a double-click (the windows of a link wait for it). */
const DOUBLE_CLICK_MS = 260
/** Width of the menu that asks which pin a dropped variable or wire goes on. */
const PICKER_WIDTH = 220

const isEditing = (target: EventTarget | null): boolean => target instanceof HTMLElement && (target.isContentEditable || !!target.closest('input, textarea, select, [contenteditable="true"]'))

interface Picker {
  readonly source: Source
  readonly block: string
  readonly label: string
  readonly sent: Sent
  /** A chip pulled off this pin: it leaves it when it goes elsewhere. */
  readonly moveFrom?: PinTarget
  /** The variable picked in the tree: used, the tree lets it go. */
  readonly fromTree?: boolean
}

export function CanvasSurface({ blocks, picked, onPickedUsed, onBackgroundClick, onToggleDetails }: {
  blocks: BlocksState
  /** The variable picked in the tree: a tap on a block offers its pins for it (no drag needed). */
  picked?: ObjectPath
  /** The picked variable went on a pin (or was let go): the tree lets it go. */
  onPickedUsed?: () => void
  onBackgroundClick?: () => void
  /** A block double-clicked: its details shown in the right panel, or hidden when they are shown. Selecting alone opens nothing. */
  onToggleDetails?: (block: string) => void
}) {
  const { ctx, program, layout, selectedIds } = blocks
  const list = program.blocks
  const errors = useErrors()
  const surfaceRef = useRef<HTMLDivElement>(null)
  const [viewport, setViewportState] = useState<Viewport>(blocks.viewport ?? { x: 0, y: 0, zoom: 1 })
  const viewportRef = useRef(viewport)
  const setViewport = (next: Viewport) => {
    viewportRef.current = next
    setViewportState(next)
  }
  const saveTimer = useRef<number | undefined>(undefined)
  const save = (delay = 0) => {
    window.clearTimeout(saveTimer.current)
    saveTimer.current = window.setTimeout(() => blocks.setViewport(viewportRef.current), delay)
  }
  const [ghost, setGhost] = useState<{ at: Point; shape?: BlockShape }>()
  const pressAt = useRef<Point | undefined>(undefined)
  const pointers = useRef(new Map<number, Point>())
  const [panning, setPanning] = useState(false)
  const [cursor, setCursor] = useState<Point>()
  const [draft, setDraft] = useState<{ from: { block: string; pin: BlockPin }; at: Point }>()
  const [picker, setPicker] = useState<Picker>()
  const [selectedWire, setSelectedWire] = useState<Wire>()
  const [chipPress, setChipPress] = useState<{ target: PinTarget; path: ObjectPath; start: Point; pulled: boolean }>()
  const [chipGhost, setChipGhost] = useState<{ at: Point; label: string }>()
  const [selectedChip, setSelectedChip] = useState<PinTarget>()
  const [selectedFree, setSelectedFree] = useState<string>()
  const [chipDraft, setChipDraft] = useState<{ id: string; at: Point }>()
  const [link, setLink] = useState<{ from: string; to: string; source: number }>()
  const [marquee, setMarquee] = useState<{ from: Point; to: Point }>()
  const [groupDrag, setGroupDrag] = useState<{ from: string; by: Point }>()
  const tapAction = useRef<{ id: string; kind: 'toggle' | 'only' | 'link' | 'variable'; from?: string } | undefined>(undefined)
  // A tap that links two blocks opens the windows once a double-click can no longer follow: the windows stand over the tapped
  // block, and the second click of a double-click (show its details) must still land on the block.
  const linkTimer = useRef<number | undefined>(undefined)

  const shapes = useMemo(() => new Map(list.map((b) => [b.id, blockShape(ctx, b)])), [list, ctx])
  const shapeOf = (b: BlockInstance) => shapes.get(b.id) ?? blockShape(ctx, b)
  const place = (id: string) => positionOf(layout, list, id)
  // The selected block, for the resize observer (it reads it when a panel opens, after this render).
  const latest = useRef<{ selected?: { at: Point; width: number } }>({})
  const selectedBlock = selectedIds.length === 1 ? list.find((b) => b.id === selectedIds[0]) : undefined
  latest.current = { selected: selectedBlock && { at: place(selectedBlock.id), width: shapeOf(selectedBlock).width } }
  const byId = (id: string) => list.find((b) => b.id === id)
  const label = (s: Source) => sourceLabel(ctx, s)

  // First shown in this session: centred on the blocks (on the origin when there are none).
  useLayoutEffect(() => {
    if (blocks.viewport) return
    const surface = surfaceRef.current
    if (!surface) return
    let middle = { x: 0, y: 0 }
    if (list.length) {
      const boxes = list.map((b) => ({ at: place(b.id), shape: shapeOf(b) }))
      const left = Math.min(...boxes.map((b) => b.at.x))
      const top = Math.min(...boxes.map((b) => b.at.y))
      const right = Math.max(...boxes.map((b) => b.at.x + b.shape.width))
      const bottom = Math.max(...boxes.map((b) => b.at.y + b.shape.height))
      middle = { x: (left + right) / 2, y: (top + bottom) / 2 }
    }
    setViewport({ zoom: 1, x: Math.round(surface.clientWidth / 2 - middle.x), y: Math.round(surface.clientHeight / 2 - middle.y) })
    save()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  // The middle of the view, for blocks added by a click in the palette.
  useEffect(() => {
    const surface = surfaceRef.current
    if (!surface || typeof ResizeObserver === 'undefined') return
    // The surface narrows and widens with the panels beside it: the blocks stay where they are on the screen. Only a move of the
    // surface's own corner (the left panel resized) is made up for; a panel opening on the right just covers less or more.
    // The selected block is the exception: a panel that would cover it moves the view just enough to keep it in sight.
    let corner = surface.getBoundingClientRect()
    const observer = new ResizeObserver(() => {
      const before = blocks.surfaceSize.current
      const size = { width: surface.clientWidth, height: surface.clientHeight }
      blocks.surfaceSize.current = size
      const now = surface.getBoundingClientRect()
      const dx = now.left - corner.left
      const dy = now.top - corner.top
      corner = now
      let view = viewportRef.current
      if (dx || dy) view = { ...view, x: view.x - dx, y: view.y - dy }
      const keep = latest.current.selected
      if (keep && size.width < before.width) {
        const left = keep.at.x * view.zoom + view.x
        const right = (keep.at.x + keep.width) * view.zoom + view.x
        const over = right - (size.width - 16)
        if (over > 0) view = { ...view, x: view.x - Math.min(over, Math.max(0, left - 16)) }
      }
      if (view === viewportRef.current) return
      setViewport(view)
      save(200)
    })
    observer.observe(surface)
    return () => observer.disconnect()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [])

  const clearPicks = () => {
    setSelectedWire(undefined)
    setSelectedChip(undefined)
    setSelectedFree(undefined)
  }

  const removeSelectedWire = (): boolean => {
    if (selectedFree) {
      blocks.removeChip(selectedFree)
      setSelectedFree(undefined)
      return true
    }
    if (selectedChip) {
      blocks.update(selectedChip.block, (b) => disconnect(b, selectedChip))
      setSelectedChip(undefined)
      return true
    }
    if (!selectedWire) return false
    blocks.update(selectedWire.to.block, (b) => disconnect(b, selectedWire.to))
    setSelectedWire(undefined)
    return true
  }

  // Delete removes the selected wire, chip or blocks; Escape clears the selection and any wire in progress; Ctrl+C / Ctrl+V copy
  // and paste blocks (not while typing in a field).
  useEffect(() => {
    const onKey = (event: KeyboardEvent) => {
      if (isEditing(event.target)) return
      if (event.key === 'Escape') {
        window.clearTimeout(linkTimer.current)
        if (picked && !picker) onPickedUsed?.()
        blocks.arm(undefined)
        setDraft(undefined)
        setPicker(undefined)
        setLink(undefined)
        setChipDraft(undefined)
        clearPicks()
        blocks.select(undefined)
      } else if (event.key === 'Delete' || event.key === 'Backspace') {
        if (event.ctrlKey || event.metaKey || event.altKey) return
        if (removeSelectedWire()) event.preventDefault()
        else if (selectedIds.length) {
          event.preventDefault()
          blocks.remove(selectedIds)
        }
      } else if ((event.ctrlKey || event.metaKey) && !event.altKey && !event.shiftKey) {
        const key = event.key.toLowerCase()
        if (key === 'c' && selectedIds.length && !window.getSelection()?.toString()) {
          event.preventDefault()
          blocks.copy()
        } else if (key === 'v' && blocks.canPaste) {
          event.preventDefault()
          blocks.paste()
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

  // Wheel zoom needs a listener that is not passive, to keep the page from scrolling.
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
  }, [])

  const blockAt = (point: Point, except?: string): BlockInstance | undefined =>
    [...list].reverse().find((b) => {
      if (b.id === except) return false
      const at = place(b.id)
      const shape = shapeOf(b)
      return point.x >= at.x - 12 && point.x <= at.x + shape.width + 12 && point.y >= at.y && point.y <= at.y + shape.height
    })

  /** The pin (an input, an output or the Run when strip) under a screen point, by the data attributes the block view puts on them. */
  const pinAtPoint = (clientX: number, clientY: number, except?: string): PinTarget | undefined => {
    for (const element of document.elementsFromPoint(clientX, clientY)) {
      const pin = element instanceof HTMLElement ? element.closest<HTMLElement>('[data-pin-kind]') : null
      const block = pin?.dataset.block
      if (!pin || !block || block === except) continue
      const kind = pin.dataset.pinKind
      if (kind === 'en') return { block, side: 'en' }
      if (kind === 'in' || kind === 'out') return { block, side: kind, index: Number(pin.dataset.pinIndex) }
    }
    return undefined
  }

  /** Whether a source goes on this pin. */
  const takes = (target: PinTarget, source: Source): boolean => {
    const block = byId(target.block)
    if (!block) return false
    const pins = pinsOf(ctx.catalog, block)
    const pin = target.side === 'in' ? pins?.inputs[target.index] : target.side === 'out' ? pins?.outputs[target.index] : undefined
    return fits(pin, target.side, sentBy(ctx, source), source)
  }

  /**
   * A variable let go at a screen point. On a pin that takes it, it goes on with no menu (a chip from another pin leaves that one,
   * a free chip docks and goes); on a block but not on a pin, the pins are offered (unless `ask` is false); false: the open canvas.
   */
  const dropVariable = (path: ObjectPath, clientX: number, clientY: number, options: { moveFrom?: PinTarget; fromFree?: string; ask?: boolean } = {}): boolean => {
    const source: Source = { kind: 'variable', path }
    const hit = pinAtPoint(clientX, clientY)
    if (hit && takes(hit, source)) {
      const { moveFrom, fromFree } = options
      blocks.edit((p) => ({
        ...p,
        program: { ...p.program, blocks: p.program.blocks.map((b) => {
          let next = b
          if (moveFrom && !sameTarget(moveFrom, hit) && b.id === moveFrom.block) next = disconnect(next, moveFrom)
          if (b.id === hit.block) next = connect(next, hit, source)
          return next
        }) },
        canvas: fromFree ? { ...p.canvas, chips: p.canvas.chips.filter((c) => c.id !== fromFree) } : p.canvas,
      }))
      return true
    }
    if (options.ask === false) return false
    const over = blockAt(screenToCanvas(viewportRef.current, local(clientX, clientY)))
    if (!over) return false
    setPicker({ source, block: over.id, label: label(source), sent: sentBy(ctx, source), ...(options.moveFrom ? { moveFrom: options.moveFrom } : {}) })
    return true
  }

  /** Where a free chip lands when it is let go on the open canvas: the pointer holds its middle. */
  const chipSpot = (clientX: number, clientY: number): Point => {
    const at = screenToCanvas(viewportRef.current, local(clientX, clientY))
    return snapPoint({ x: at.x - CHIP_WIDTH / 2, y: at.y - CHIP_HEIGHT / 2 })
  }

  const onPointerDown = (event: React.PointerEvent<HTMLDivElement>) => {
    if (event.button !== 0 && event.button !== 1) return
    // Blocks, the zoom buttons, the windows and the messages handle their own presses.
    if ((event.target as HTMLElement).closest('.canvas-block, .canvas-zoom, .canvas-hint, .wire-picker, .canvas-chip')) return
    setDraft(undefined)
    setPicker(undefined)
    setLink(undefined)
    clearPicks()
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
        setChipGhost({ at: point, label: chipLabel(label({ kind: 'variable', path: chipPress.path })) })
      }
      return
    }
    if (chipDraft) return setChipDraft({ ...chipDraft, at: screenToCanvas(viewportRef.current, point) })
    if (draft) return setDraft({ ...draft, at: screenToCanvas(viewportRef.current, point) })
    const previous = pointers.current.get(event.pointerId)
    if (!previous) return
    const others = [...pointers.current].filter(([id]) => id !== event.pointerId).map(([, at]) => at)
    pointers.current.set(event.pointerId, point)
    if (marquee) {
      if (others.length === 0) return setMarquee({ from: marquee.from, to: point })
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
    const inside = list.filter((block) => {
      const at = place(block.id)
      const shape = shapeOf(block)
      return at.x < to.x && at.x + shape.width > from.x && at.y < to.y && at.y + shape.height > from.y
    })
    blocks.selectMany(inside.map((block) => block.id), true)
  }

  const onPointerEnd = (event: React.PointerEvent<HTMLDivElement>) => {
    if (chipPress) {
      const press = chipPress
      setChipPress(undefined)
      setChipGhost(undefined)
      if (!press.pulled) {
        // A click: the chip is selected (Delete takes it off) and its block's details open.
        blocks.select(press.target.block)
        setSelectedChip(press.target)
        setSelectedWire(undefined)
        return
      }
      // Pulled onto a pin: it moves there. Onto a block: its pins are offered. Onto the open canvas: it comes off as a free chip,
      // still wired to the pin.
      if (!dropVariable(press.path, event.clientX, event.clientY, { moveFrom: press.target })) blocks.addChip(press.path, chipSpot(event.clientX, event.clientY))
      return
    }
    if (chipDraft) {
      const chip = layout.chips.find((c) => c.id === chipDraft.id)
      setChipDraft(undefined)
      if (chip) dropVariable(chip.path, event.clientX, event.clientY)
      return
    }
    if (draft) {
      // A wire let go: on a pin that takes it, it connects; on a block, the pins are offered; anywhere else it is dropped.
      const source = blockSource(draft.from.block, draft.from.pin)
      const hit = pinAtPoint(event.clientX, event.clientY, draft.from.block)
      if (hit && hit.side !== 'out' && takes(hit, source)) {
        blocks.update(hit.block, (b) => connect(b, hit, source))
        setDraft(undefined)
        return
      }
      const over = blockAt(screenToCanvas(viewportRef.current, local(event.clientX, event.clientY)), draft.from.block)
      if (over) setPicker({ source, block: over.id, label: label(source), sent: sentBy(ctx, source) })
      setDraft(undefined)
      return
    }
    const press = pressAt.current
    if (press && pointers.current.size === 1 && pointers.current.has(event.pointerId)) {
      const end = local(event.clientX, event.clientY)
      if (Math.hypot(end.x - press.x, end.y - press.y) < CLICK_SLOP) {
        // A block picked in the palette lands where the canvas is clicked (the pointer holds its header).
        if (blocks.armed) {
          const under = screenToCanvas(viewportRef.current, end)
          blocks.add(blocks.armed, snapPoint({ x: under.x - GRAB.x, y: under.y - GRAB.y }))
          blocks.arm(undefined)
        } else {
          // A click on the empty canvas clears the selection, and lets go of a variable picked in the tree.
          blocks.select(undefined)
          if (picked) onPickedUsed?.()
          onBackgroundClick?.()
        }
      } else if (marquee) selectInside(marquee.from, end)
    }
    pressAt.current = undefined
    pointers.current.delete(event.pointerId)
    setMarquee(undefined)
    if (!pointers.current.size) {
      setPanning(false)
      save()
    }
  }

  const startWire = (block: string, pin: BlockPin, event: React.PointerEvent) => {
    surfaceRef.current?.setPointerCapture(event.pointerId)
    setSelectedWire(undefined)
    setPicker(undefined)
    setDraft({ from: { block, pin }, at: screenToCanvas(viewportRef.current, local(event.clientX, event.clientY)) })
  }

  const pick = (target: PinTarget) => {
    if (!picker) return
    const from = picker.moveFrom
    blocks.edit((p) => ({
      ...p,
      program: { ...p.program, blocks: p.program.blocks.map((b) => {
        let next = b
        if (from && !sameTarget(from, target) && b.id === from.block) next = disconnect(next, from)
        if (b.id === target.block) next = connect(next, target, picker.source)
        return next
      }) },
    }))
    if (picker.fromTree) onPickedUsed?.()
    setPicker(undefined)
  }

  /** Selecting a block: a plain press selects only it, Shift / Ctrl toggles it in the group. */
  const onBlockSelect = (block: BlockInstance, additive: boolean) => {
    clearPicks()
    setLink(undefined)
    const inGroup = selectedIds.includes(block.id)
    // A variable is picked in the tree: a tap offers the block's pins for it (a drag just moves the block).
    if (!additive && picked) {
      tapAction.current = { id: block.id, kind: 'variable' }
      return
    }
    // One block is selected and another is pressed: a tap links them (a drag just moves the pressed one).
    if (!additive && selectedIds.length === 1 && selectedIds[0] !== block.id) {
      tapAction.current = { id: block.id, kind: 'link', from: selectedIds[0] }
      return
    }
    if (additive) {
      blocks.toggle(block.id)
      tapAction.current = undefined
    } else if (inGroup && selectedIds.length > 1) tapAction.current = { id: block.id, kind: 'only' }
    else blocks.select(block.id)
  }

  // A block dragged from the palette: where its corner would land.
  const dropPoint = (event: React.DragEvent): Point => {
    const under = screenToCanvas(viewport, local(event.clientX, event.clientY))
    return snapPoint({ x: under.x - GRAB.x, y: under.y - GRAB.y })
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
  // Dots on the grid points (each layer shifted half a tile so a dot sits on the origin); the minor dots drop out when they would crowd.
  const layers = [{ size: cell * MAJOR, color: 'var(--canvas-dot-major)', radius: 1.4 }, ...(cell >= 8 ? [{ size: cell, color: 'var(--canvas-dot)', radius: 1 }] : [])]
  const gridStyle: CSSProperties = {
    backgroundImage: layers.map((l) => `radial-gradient(circle, ${l.color} ${l.radius}px, transparent ${l.radius + 0.4}px)`).join(', '),
    backgroundSize: layers.map((l) => `${l.size}px ${l.size}px`).join(', '),
    backgroundPosition: layers.map((l) => `${viewport.x - l.size / 2}px ${viewport.y - l.size / 2}px`).join(', '),
  }
  const pointed = cursor && screenToCanvas(viewport, cursor)
  const marker = pointed && canvasToScreen(viewport, snapPoint(pointed))
  // A block picked in the palette: its outline follows the pointer until a click places it.
  const armedType = blocks.armed && ctx.catalog.get(blocks.armed.type)
  const armedShape = armedType && blockShape(ctx, newBlock('', armedType.id, blocks.armed?.variant ? { variant: blocks.armed.variant } : {}))
  const armedAt = pointed && snapPoint({ x: pointed.x - GRAB.x, y: pointed.y - GRAB.y })

  // The link windows: what the first block sends (each output, and When done), and where in the second it can go.
  const linkFrom = link ? byId(link.from) : undefined
  const linkTo = link ? byId(link.to) : undefined
  const sourcesOf = (block: BlockInstance): SourceChoice[] => [
    ...(pinsOf(ctx.catalog, block)?.outputs ?? []).map((p) => ({
      key: `q${p.index}`, label: p.pin.alias, value: p.pin.value,
      detail: block.outputs[p.index] ? `also writes ${label({ kind: 'variable', path: block.outputs[p.index]!.path })}` : p.pin.description ?? 'An output of the block',
    })),
    { key: 'eno', label: blockPinName(ctx.catalog, block, 'eno'), value: 'flow', detail: ctx.catalog.get(block.type)?.eno?.description ?? 'True while the block acted' },
  ]
  const linkSources = linkFrom ? sourcesOf(linkFrom) : []
  const chosen = link ? Math.min(link.source, Math.max(0, linkSources.length - 1)) : 0
  const sent = linkSources[chosen]
  const sentSource = linkFrom && sent ? blockSource(linkFrom.id, sent.key === 'eno' ? 'eno' : Number(sent.key.slice(1))) : undefined
  const linkRows: PickerRow[] = linkTo && sentSource ? [
    { key: 'en', label: 'Run when', detail: linkTo.enables.length ? `also ${linkTo.enables.map(label).join(', ')}` : 'the block runs only while this is true', value: 'flow', target: { block: linkTo.id, side: 'en' }, ok: takes({ block: linkTo.id, side: 'en' }, sentSource) },
    ...(pinsOf(ctx.catalog, linkTo)?.inputs ?? []).map((p): PickerRow => {
      const current = linkTo.inputs[p.index]
      const target: PinTarget = { block: linkTo.id, side: 'in', index: p.index }
      return { key: `in${p.index}`, label: p.pin.alias, detail: current ? `now ${label(current)}` : 'Unwired', value: p.pin.value, target, ok: takes(target, sentSource) }
    }),
  ] : []
  const linkInto = (row: PickerRow) => {
    if (!link || !sentSource) return
    blocks.update(row.target.block, (b) => connect(b, row.target, sentSource))
    setLink(undefined)
  }

  const pickerBlock = picker ? byId(picker.block) : undefined
  const pickerPins = pickerBlock ? pinsOf(ctx.catalog, pickerBlock) : undefined
  // The pin menu is a window over the block, as wide as it (at least PICKER_WIDTH), kept inside the canvas.
  const pickerRows = pickerPins ? 1 + pickerPins.inputs.length + (picker?.source.kind === 'variable' ? pickerPins.outputs.length : 0) : 0
  const pickerWidth = pickerBlock ? Math.max(PICKER_WIDTH, shapeOf(pickerBlock).width * viewport.zoom) : PICKER_WIDTH
  const pickerAt = pickerBlock && (() => {
    const at = canvasToScreen(viewport, place(pickerBlock.id))
    const room = blocks.surfaceSize.current
    const inside = (value: number, size: number, total: number) => (total > 0 ? Math.max(8, Math.min(value, total - size - 8)) : value)
    return { x: inside(at.x, pickerWidth, room.width), y: inside(at.y, 31 * (pickerRows + 1), room.height) }
  })()
  const failed = (id: string) => errors?.console.aboutTarget((t) => isBlockTarget(t, id)).some((e) => !e.read) ?? false
  const problem = (id: string) => blockDiagnostics(blocks.compiled.diagnostics, id).filter((d) => d.severity === 'error').map((d) => d.message).join(' ') || undefined

  return (
    <div
      ref={surfaceRef}
      className={`canvas-surface ${panning ? 'is-panning' : ''} ${blocks.armed ? 'is-placing' : ''}`}
      style={gridStyle}
      aria-label="Canvas"
      onPointerDown={onPointerDown}
      onPointerMove={onPointerMove}
      onPointerUp={onPointerEnd}
      onPointerCancel={onPointerEnd}
      onPointerLeave={() => setCursor(undefined)}
      onDragOver={(event) => {
        if (event.dataTransfer.types.includes(VARIABLE_DRAG_TYPE)) {
          event.preventDefault()
          event.dataTransfer.dropEffect = 'copy'
          return
        }
        if (!event.dataTransfer.types.includes(BLOCK_DRAG_TYPE)) return
        event.preventDefault()
        event.dataTransfer.dropEffect = 'copy'
        const placing = blocks.paletteDrag.current
        const type = placing && ctx.catalog.get(placing.type)
        setGhost({ at: dropPoint(event), ...(type ? { shape: blockShape(ctx, newBlock('', type.id, placing.variant ? { variant: placing.variant } : {})) } : {}) })
      }}
      onDragLeave={(event) => {
        if (event.currentTarget.contains(event.relatedTarget as Node | null)) return
        setGhost(undefined)
      }}
      onDrop={(event) => {
        setGhost(undefined)
        if (event.dataTransfer.types.includes(VARIABLE_DRAG_TYPE)) {
          event.preventDefault()
          const draftPath = draftForVariable(ctx.project.variables, event.dataTransfer.getData(VARIABLE_DRAG_TYPE))
          const path = draftPath && toPath(draftPath)
          if (path && !dropVariable(path, event.clientX, event.clientY)) blocks.addChip(path, chipSpot(event.clientX, event.clientY))
          return
        }
        if (!event.dataTransfer.types.includes(BLOCK_DRAG_TYPE)) return
        event.preventDefault()
        const placement = decodePlacement(event.dataTransfer.getData(BLOCK_DRAG_TYPE))
        blocks.paletteDrag.current = undefined
        if (placement.type) blocks.add(placement, dropPoint(event))
      }}
    >
      <div className="canvas-world" style={{ transform: `translate(${viewport.x}px, ${viewport.y}px) scale(${viewport.zoom})` }}>
        <div className="canvas-origin" aria-hidden="true" />
        <CanvasWires
          blocks={list}
          layout={layout}
          place={place}
          shapeOf={shapeOf}
          selected={selectedWire}
          selectedPin={selectedChip}
          draft={draft && { from: draft.from, to: draft.at }}
          chipDraft={chipDraft && (() => {
            const chip = layout.chips.find((c) => c.id === chipDraft.id)
            return chip && { from: { x: chip.x + CHIP_WIDTH, y: chip.y + CHIP_HEIGHT / 2 }, to: chipDraft.at }
          })()}
          onSelect={(wire) => { clearPicks(); setSelectedWire(wire); blocks.select(undefined) }}
          onSelectPin={(target) => { clearPicks(); setSelectedChip(target); blocks.select(undefined) }}
        />
        {list.map((block) => (
          <CanvasBlockView
            key={block.id}
            block={block}
            ctx={ctx}
            category={ctx.catalog.get(block.type)?.category ?? 'unknown'}
            at={place(block.id)}
            shape={shapeOf(block)}
            selected={selectedIds.includes(block.id)}
            failed={failed(block.id)}
            problem={problem(block.id)}
            zoom={viewport.zoom}
            onSelect={(additive) => onBlockSelect(block, additive)}
            onTap={() => {
              const action = tapAction.current
              tapAction.current = undefined
              if (action?.id !== block.id) return
              if (action.kind === 'link' && action.from) {
                const from = action.from
                window.clearTimeout(linkTimer.current)
                linkTimer.current = window.setTimeout(() => setLink({ from, to: block.id, source: 0 }), DOUBLE_CLICK_MS)
              }
              else if (action.kind === 'variable' && picked) {
                const source: Source = { kind: 'variable', path: picked }
                window.clearTimeout(linkTimer.current)
                linkTimer.current = window.setTimeout(() => setPicker({ source, block: block.id, label: label(source), sent: sentBy(ctx, source), fromTree: true }), DOUBLE_CLICK_MS)
              }
              else if (action.kind === 'toggle') blocks.toggle(block.id)
              else blocks.select(block.id)
            }}
            onMove={(to) => {
              // Dragging the pressed block (not tapping it) makes it the selection.
              if (tapAction.current?.kind === 'link') blocks.select(block.id)
              tapAction.current = undefined
              // A block of a selected group takes the whole group along, as one undo step.
              const at = place(block.id)
              if (selectedIds.length > 1 && selectedIds.includes(block.id)) blocks.moveBy(selectedIds, { x: to.x - at.x, y: to.y - at.y })
              else blocks.moveTo({ [block.id]: to })
            }}
            onDrag={(by) => setGroupDrag(by && selectedIds.length > 1 && selectedIds.includes(block.id) ? { from: block.id, by } : undefined)}
            offset={groupDrag && groupDrag.from !== block.id && selectedIds.includes(block.id) ? groupDrag.by : undefined}
            onOpen={() => {
              window.clearTimeout(linkTimer.current)
              tapAction.current = undefined
              setLink(undefined)
              blocks.select(block.id)
              onToggleDetails?.(block.id)
            }}
            onWireStart={(pin, event) => startWire(block.id, pin, event)}
            isFree={(path) => !!chipFor(layout, path)}
            selectedChip={selectedChip?.block === block.id ? selectedChip : undefined}
            onChipPress={(target, path, event) => {
              surfaceRef.current?.setPointerCapture(event.pointerId)
              setPicker(undefined)
              setChipPress({ target, path, start: { x: event.clientX, y: event.clientY }, pulled: false })
            }}
          />
        ))}
        {layout.chips.map((chip) => {
          const full = label({ kind: 'variable', path: chip.path })
          return (
            <FreeChip
              key={chip.id}
              chip={chip}
              label={chipLabel(full)}
              full={full}
              selected={selectedFree === chip.id}
              zoom={viewport.zoom}
              onSelect={() => { clearPicks(); setSelectedFree(chip.id); blocks.select(undefined) }}
              onMove={(to) => blocks.moveChip(chip.id, to)}
              onDropAt={(x, y) => dropVariable(chip.path, x, y, { fromFree: chip.id, ask: false })}
              onWireStart={(event) => {
                surfaceRef.current?.setPointerCapture(event.pointerId)
                setPicker(undefined)
                setChipDraft({ id: chip.id, at: screenToCanvas(viewportRef.current, local(event.clientX, event.clientY)) })
              }}
            />
          )
        })}
        {armedShape && armedAt && !ghost && <div className="canvas-block-ghost is-armed" aria-hidden="true" style={{ left: armedAt.x, top: armedAt.y, width: armedShape.width, height: armedShape.height }}><span>{armedType!.variants.find((v) => v.id === blocks.armed?.variant)?.alias ?? armedType!.alias}</span></div>}
        {ghost && <div className="canvas-block-ghost" aria-hidden="true" style={{ left: ghost.at.x, top: ghost.at.y, width: ghost.shape?.width ?? 6 * GRID, height: ghost.shape?.height ?? 3 * GRID }} />}
      </div>
      {chipGhost && <div className="canvas-chip is-ghost" aria-hidden="true" style={{ left: chipGhost.at.x, top: chipGhost.at.y }}>{chipGhost.label}</div>}
      {picker && pickerBlock && pickerPins && pickerAt && (
        <OptionMenu className="wire-picker is-target" aria-label={`Connect to ${blocks.nameOf(pickerBlock.id)}`} style={{ left: pickerAt.x, top: pickerAt.y, width: pickerWidth }} onCancel={() => setPicker(undefined)}>
          <div className="wire-picker-head">
            <span title={`${picker.label} → ${blocks.nameOf(pickerBlock.id)}`}>{picker.label} → {blocks.nameOf(pickerBlock.id)}</span>
            <button type="button" className="wire-picker-icon" title="Cancel" aria-label="Cancel" onClick={() => setPicker(undefined)}><X aria-hidden="true" /></button>
          </div>
          {picker.sent !== 'object' && (
            <MenuOption className="wire-picker-row is-en" title="The block runs only while this is true" onChoose={() => pick({ block: pickerBlock.id, side: 'en' })}>
              <TypeLead kind="bool" /><span className="wire-picker-label">Run when</span>
            </MenuOption>
          )}
          {pickerPins.inputs.map((p) => {
            const target: PinTarget = { block: pickerBlock.id, side: 'in', index: p.index }
            const now = pickerBlock.inputs[p.index]
            return (
              <MenuOption key={`in${p.index}`} className="wire-picker-row" disabled={!takes(target, picker.source)} title={now ? `Now ${label(now)}` : 'Unwired'} onChoose={() => pick(target)}>
                <TypeLead kind={pinLead(p.pin.value)} /><span className="wire-picker-label">{p.pin.alias}</span>
              </MenuOption>
            )
          })}
          {picker.source.kind === 'variable' && pickerPins.outputs.map((p) => {
            const target: PinTarget = { block: pickerBlock.id, side: 'out', index: p.index }
            return (
              <MenuOption key={`out${p.index}`} className="wire-picker-row is-output" disabled={!takes(target, picker.source)} title="The block writes the variable" onChoose={() => pick(target)}>
                <TypeLead kind={pinLead(p.pin.value)} /><span className="wire-picker-label">{p.pin.alias}</span><span className="wire-picker-detail">writes it</span>
              </MenuOption>
            )
          })}
        </OptionMenu>
      )}
      {link && linkFrom && linkTo && sent && (
        <LinkWindows
          fromName={blocks.nameOf(linkFrom.id)}
          fromAt={place(linkFrom.id)}
          fromShape={shapeOf(linkFrom)}
          toName={blocks.nameOf(linkTo.id)}
          toAt={place(linkTo.id)}
          toShape={shapeOf(linkTo)}
          sources={linkSources}
          chosen={chosen}
          rows={linkRows}
          viewport={viewport}
          bounds={blocks.surfaceSize.current}
          onChoose={(index) => setLink({ ...link, source: index })}
          onPick={linkInto}
          onSwap={() => setLink({ from: link.to, to: link.from, source: 0 })}
          onSelect={() => { blocks.select(link.to); setLink(undefined) }}
          onCancel={() => setLink(undefined)}
        />
      )}
      {marquee && <div className="canvas-marquee" aria-hidden="true" style={{ left: Math.min(marquee.from.x, marquee.to.x), top: Math.min(marquee.from.y, marquee.to.y), width: Math.abs(marquee.to.x - marquee.from.x), height: Math.abs(marquee.to.y - marquee.from.y) }} />}
      {(selectedWire || selectedChip || selectedFree) && <div className="canvas-wire-hint">{selectedFree ? 'Chip selected: Delete removes it from the canvas (the pins keep the variable)' : 'Selected: Delete takes it off, Esc keeps it'}</div>}
      {marker && !panning && !ghost && !draft && !blocks.armed && <div className="canvas-snap-marker" aria-hidden="true" style={{ left: marker.x, top: marker.y }} />}
      {picked && !blocks.armed && !picker && <div className="canvas-hint is-placing" aria-hidden="true">Click a block to put {label({ kind: 'variable', path: picked })} on one of its pins · a click on the empty canvas lets it go</div>}
      {blocks.armed && armedType && <div className="canvas-hint is-placing" aria-hidden="true">Click where {armedType.alias} goes · Esc puts it back</div>}
      {list.length === 0 && !ghost && !blocks.armed && <div className="canvas-hint" aria-hidden="true">Pick a block in the palette (Blocks, on the left), then click here; or drag it here · drag the background to move, scroll or pinch to zoom</div>}
      <div className="canvas-zoom" role="toolbar" aria-label="Zoom">
        <button type="button" aria-label="Zoom out" title="Zoom out" onClick={() => zoomButton(1 / BUTTON_ZOOM)}><Minus aria-hidden="true" /></button>
        <button type="button" className="canvas-zoom-value" title="Back to 100 %" onClick={() => zoomButton(1 / viewport.zoom)}>{`${Math.round(viewport.zoom * 100)}%`}</button>
        <button type="button" aria-label="Zoom in" title="Zoom in" onClick={() => zoomButton(BUTTON_ZOOM)}><Plus aria-hidden="true" /></button>
        <button type="button" aria-label="Back to the origin" title="Back to the origin at 100 %" onClick={recenter}><Crosshair aria-hidden="true" /></button>
      </div>
    </div>
  )
}
