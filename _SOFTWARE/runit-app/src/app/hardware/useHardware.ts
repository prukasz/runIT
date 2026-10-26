import { useCallback, useMemo, useState } from 'react'
import type { useProject } from '../project/useProject'
import type { useDevices } from '../devices/useDevices'
import { applyHardware, contextOf, hardwareOf, placePart, removeItems } from './apply'
import type { ApplyContext } from './apply'
import { placedSize, turnPlacement } from './geometry'
import type { Viewport } from './geometry'
import type { HardwareLayout, PowerChoices } from './layout'
import { BOARD_ITEM, freshId } from './layout'
import type { LibraryPart, Point, Rotation, StoredPart } from './library'
import { boardPartFor, bundledLibrary } from './library'
import { buildNetlist } from './netlist'
import type { PinNode } from './netlist'
import { solvePower } from './power'
import { canWire, verdictsFor } from './wiring'
import type { Scene, Verdict } from './wiring'

/*
 * The Devices canvas's state for the screens: the scene (the library with the project's parts, the nets, the power), what is
 * selected (a part, a wire, a pin), the pin picked to wire from (every other pin then says whether it may be wired to it), the
 * part picked in the palette to place and its turn, the view (this session's), the creator. Every change of the canvas is an edit of the
 * project through `applyHardware`, so what it decides for the devices is in the same undo step.
 */

export type HardwareSelection =
  | { readonly kind: 'item'; readonly id: string }
  | { readonly kind: 'wire'; readonly id: string }
  | { readonly kind: 'pin'; readonly key: string }

/** The palette's drag type: a part's id. */
export const PART_DRAG_TYPE = 'application/x-runit-part'

export function useHardware(project: ReturnType<typeof useProject>, devices: ReturnType<typeof useDevices>) {
  const library = useMemo(() => bundledLibrary(), [])
  const applyCtx: ApplyContext = useMemo(() => ({ catalog: devices.catalog, board: devices.board, library }), [devices.catalog, devices.board, library])
  const layout = hardwareOf(project.project)
  const ctx = useMemo(() => contextOf(project.project, applyCtx), [project.project, applyCtx])
  const net = useMemo(() => buildNetlist(layout, ctx), [layout, ctx])
  const power = useMemo(() => solvePower(net, layout.power), [net, layout.power])
  const scene: Scene = useMemo(() => ({ net, power, ctx }), [net, power, ctx])
  const boardPart = boardPartFor(ctx.library, ctx.board)

  const [selection, setSelection] = useState<HardwareSelection | undefined>()
  const [picked, setPicked] = useState<string | undefined>()
  const [armed, setArmed] = useState<string | undefined>()
  const [armedTurn, setArmedTurn] = useState<Rotation>(0)
  /** Picks a palette part to place (upright), or puts it back. */
  const arm = useCallback((id: string | undefined) => { setArmed(id); setArmedTurn(0) }, [])
  const [viewport, setViewport] = useState<Viewport | undefined>()
  const [note, setNote] = useState<string | undefined>()
  const [creator, setCreator] = useState<{ readonly part?: StoredPart } | undefined>()

  const pickedNode = picked ? net.nodes.get(picked) : undefined
  const verdicts = useMemo(() => (pickedNode ? verdictsFor(scene, pickedNode) : undefined), [scene, pickedNode])

  const { edit } = project
  const change = useCallback((fn: (hw: HardwareLayout) => HardwareLayout, key?: string) =>
    edit((p) => applyHardware({ ...p, hardware: fn(hardwareOf(p)) }, applyCtx), key), [edit, applyCtx])

  /** Places a part at a canvas point (its centre there), turned as the armed part is. */
  const place = useCallback((part: LibraryPart, at: Point) => {
    const size = placedSize(part, armedTurn)
    edit((p) => placePart(p, part, { x: Math.round(at.x - size.w / 2), y: Math.round(at.y - size.h / 2) }, applyCtx, armedTurn).project)
    arm(undefined)
    // The id placePart gives it (the edit runs later, on the same items).
    setSelection({ kind: 'item', id: freshId(layout.items.map((i) => i.id), 'part') })
  }, [edit, applyCtx, layout.items, armedTurn, arm])

  const move = useCallback((id: string, to: Point) => change((hw) => (id === BOARD_ITEM
    ? { ...hw, board: { ...hw.board, x: to.x, y: to.y } }
    : { ...hw, items: hw.items.map((i) => (i.id === id ? { ...i, x: to.x, y: to.y } : i)) }), `hw-move:${id}`), [change])

  /** Turns a part or the board a quarter about its centre (90 clockwise, -90 the other way). */
  const rotate = useCallback((id: string, by: 90 | -90 = 90) => {
    const placed = net.parts.find((p) => p.id === id)
    if (!placed) return
    const to = turnPlacement(placed.part, placed, by)
    change((hw) => (id === BOARD_ITEM
      ? { ...hw, board: to }
      : { ...hw, items: hw.items.map((i) => (i.id === id ? { ...i, ...to } : i)) }))
  }, [net.parts, change])

  /** Turns the selected part, or the one being placed. */
  const turnSelected = useCallback((by: 90 | -90 = 90) => {
    if (armed) setArmedTurn((r) => ((r + by + 360) % 360) as Rotation)
    else if (selection?.kind === 'item') rotate(selection.id, by)
  }, [armed, selection, rotate])

  const remove = useCallback((sel: HardwareSelection | undefined = selection) => {
    if (!sel) return
    if (sel.kind === 'item' && sel.id !== BOARD_ITEM) edit((p) => removeItems(p, [sel.id], applyCtx))
    if (sel.kind === 'wire') change((hw) => ({ ...hw, wires: hw.wires.filter((w) => w.id !== sel.id) }))
    if (sel.kind === 'pin') change((hw) => ({ ...hw, wires: hw.wires.filter((w) => `${w.a.item}:${w.a.pin}` !== sel.key && `${w.b.item}:${w.b.pin}` !== sel.key) }))
    setSelection(undefined)
  }, [selection, edit, change, applyCtx])

  /** Wires two pins when the rules let it; answers the verdict either way (the line under the canvas says it). */
  const connect = useCallback((from: PinNode, to: PinNode): Verdict => {
    const verdict = canWire(scene, from, to)
    if (verdict.ok) {
      change((hw) => ({ ...hw, wires: [...hw.wires, { id: freshId(hw.wires.map((w) => w.id), 'wire'), a: from.end, b: to.end }] }))
      setNote(verdict.note)
    } else setNote(verdict.why)
    setPicked(undefined)
    return verdict
  }, [scene, change])

  /** A tap on a pin: picks it, or wires the picked one to it, or lets it go. */
  const tapPin = useCallback((node: PinNode) => {
    setSelection({ kind: 'pin', key: node.key })
    if (pickedNode && pickedNode.key !== node.key) {
      connect(pickedNode, node)
      return
    }
    setPicked(pickedNode?.key === node.key ? undefined : node.key)
    setNote(undefined)
  }, [pickedNode, connect])

  const setPower = useCallback((fn: (p: PowerChoices) => PowerChoices) => change((hw) => ({ ...hw, power: fn(hw.power) })), [change])

  /** A part made in the creator: kept in the project (it travels with it), replacing one of the same id. */
  const keepPart = useCallback((part: StoredPart) => change((hw) => ({ ...hw, parts: [...hw.parts.filter((p) => p.id !== part.id), part] })), [change])

  return {
    library: ctx.library, ctx, layout, net, power, scene, boardPart,
    selection, select: setSelection, picked: pickedNode, pick: setPicked, verdicts, armed, armedTurn, arm,
    viewport, setViewport, note, setNote,
    place, move, rotate, turnSelected, canTurn: !!armed || selection?.kind === 'item', remove, connect, tapPin, setPower, keepPart,
    creator, openCreator: (part?: StoredPart) => setCreator(part ? { part } : {}), closeCreator: () => setCreator(undefined),
  }
}

export type HardwareState = ReturnType<typeof useHardware>
