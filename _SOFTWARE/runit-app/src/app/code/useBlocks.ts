import { useMemo, useRef, useState } from 'react'
import { newBlock, variantOf } from '@engine'
import type { BlockInstance, BlockType, Engine, ObjectPath } from '@engine'
import { onboardActions } from '@backend'
import { changesPin, isReserved, pinReady, pinStateOf, usedBy } from '../devices/devices'
import type { DeviceInfo } from '../devices/devices'
import type { DevicesState } from '../devices/useDevices'
import type { ProjectFile } from '../project/projectFile'
import type { useProject } from '../project/useProject'
import { blockName, copyBlocks, newBlockId, removeBlocks } from './blocks'
import type { BlockContext } from './blocks'
import { blockShape, chipFor, DEFAULT_VIEWPORT, freeSpot, GRID, newChipId, positionOf, screenToCanvas, snapPoint, withoutBlocks } from './canvas/layout'
import type { CanvasLayout, Point, Viewport } from './canvas/layout'

/** What a palette entry places: a block type, and for a device block its variant (`io#set_level`). */
export interface Placement {
  readonly type: string
  readonly variant?: string
}

export const encodePlacement = (p: Placement): string => (p.variant ? `${p.type}#${p.variant}` : p.type)
export const decodePlacement = (text: string): Placement => {
  const [type = '', variant] = text.split('#')
  return variant ? { type, variant } : { type }
}

/**
 * The pins a pin setting of a device block may take on `device`, as the device's test panel offers them for the same command: not a pin
 * the board keeps for itself, not one another device manages when the command changes the pin, and only a pin in the state the command
 * wants (`pin_state`) when the project starts, so the board does not refuse it. A block that runs no known command is offered every pin.
 */
export const blockPinChoices = (state: Pick<DevicesState, 'catalog' | 'board' | 'devices'>, type: BlockType, variant: string | undefined, field: string, device: DeviceInfo): number[] => {
  const count = device.type?.pins?.count ?? 0
  const operation = variantOf(type, variant)?.operation
  const command = operation && (operation.source === 'contract' ? state.catalog.contracts.get(operation.of) : device.type)?.commands.get(operation.command)
  const wants = command?.args?.fields.find((f) => f.id === field)?.pinState
  const users = usedBy(state.devices, device.id)
  return Array.from({ length: count }, (_, pin) => pin)
    .filter((pin) => !isReserved(state.board, device.id, pin))
    .filter((pin) => !command || !changesPin(command) || !users.some((u) => u.use.pin.pin === pin))
    .filter((pin) => pinReady(pinStateOf(device, state.board, pin, !device.type?.pins?.fixedModes), wants))
}

/** The devices a device block can work on: those that implement its contract, or are of its device type, and have a pin it may take when it takes one. */
export const deviceChoicesFor = (state: Pick<DevicesState, 'catalog' | 'board' | 'devices'>, type: BlockType, variant?: string) => {
  const pinField = variantOf(type, variant)?.fields.find((f) => f.type === 'pin' && !f.deviceField)
  return state.devices
    .filter((d) => (type.device?.contract ? d.type?.contracts.includes(type.device.contract) : type.device?.deviceType ? d.type?.id === type.device.deviceType : false))
    .filter((d) => !pinField || blockPinChoices(state, type, variant, pinField.id, d).length > 0)
}

/**
 * The Code view's blocks: the project's blocks and where they sit on the canvas, the selected ones, what the compiler says of them,
 * the clipboard, the canvas's viewport (this session's), and the edits (each one an undo step of the project).
 */
export function useBlocks(project: ReturnType<typeof useProject>, engine: Engine, devices: DevicesState) {
  const program = project.project.program
  const layout = project.project.canvas
  const catalog = engine.catalog
  const [selectedIds, setSelectedIds] = useState<readonly string[]>([])
  const [viewport, setViewport] = useState<Viewport | undefined>()
  /** Size of the canvas on screen (the surface sets it): where the middle of the view is. */
  const surfaceSize = useRef({ width: 0, height: 0 })
  /** What is being dragged from the palette (a drag carries only its data types until the drop): the canvas shows where it lands. */
  const paletteDrag = useRef<Placement | undefined>(undefined)
  /** A block picked in the palette and not placed yet: the next click on the canvas places it there. */
  const [armed, setArmed] = useState<Placement | undefined>()
  const [clipboard, setClipboard] = useState<readonly BlockInstance[]>([])
  const pasted = useRef(0)
  /** The select tool (the toolbar): a drag on the canvas background draws a box, as with Shift, instead of panning. */
  const [selecting, setSelecting] = useState(false)

  const compiled = useMemo(() => engine.compile(program), [engine, program])

  /** What the blocks' names and headers are made of: the project, and the names the Devices view gives devices, pins and actions. */
  const ctx = useMemo((): BlockContext => {
    const byId = new Map(devices.devices.map((d) => [d.id, d]))
    const actions = new Map<number, string>([...onboardActions().map((a) => [a.id, a.alias] as const), ...project.project.actions.map((a) => [a.id, a.name] as const)])
    return {
      catalog,
      project: program,
      deviceName: (id) => byId.get(id)?.name,
      pinName: devices.pinName,
      eventName: (device, code) => {
        const contracts = device === undefined ? [...devices.catalog.contracts.keys()] : byId.get(device)?.type?.contracts ?? []
        for (const c of contracts) {
          const event = devices.catalog.contracts.get(c)?.events.find((e) => e.code === code)
          if (event) return event.label
        }
        return undefined
      },
      actionName: (id) => actions.get(id),
      language: (type) => engine.expression(type),
    }
  }, [catalog, program, devices.devices, devices.pinName, devices.catalog, project.project.actions, engine])

  const selected = selectedIds.length === 1 ? program.blocks.find((b) => b.id === selectedIds[0]) : undefined

  /** Blocks and the canvas in one edit: one undo step. */
  const edit = (change: (p: ProjectFile) => ProjectFile, key?: string) => project.edit(change, key)
  const setLayout = (change: (layout: CanvasLayout) => CanvasLayout) => edit((p) => ({ ...p, canvas: change(p.canvas) }))

  const select = (id: string | undefined) => setSelectedIds(id === undefined ? [] : [id])
  const toggle = (id: string) => setSelectedIds((now) => (now.includes(id) ? now.filter((x) => x !== id) : [...now, id]))
  const selectMany = (ids: readonly string[], add = false) => setSelectedIds((now) => (add ? [...now, ...ids.filter((id) => !now.includes(id))] : [...ids]))

  /** Where the blocks sit, as rectangles, for finding a free spot. */
  const rects = () => program.blocks.map((b) => ({ ...positionOf(layout, program.blocks, b.id), ...blockShape(ctx, b) }))

  /**
   * A new block of this type (and variant), selected: at `at` (a drop on the canvas), or near the middle of the view, moved down past
   * what it would cover. A device block with one device that fits starts on it.
   */
  const add = (placement: Placement, at?: Point) => {
    const type = catalog.get(placement.type)
    if (!type) return
    const id = newBlockId(program.blocks, type.id)
    const fitting = type.integration === 'core' ? [] : deviceChoicesFor(devices, type, placement.variant)
    const block = newBlock(id, type.id, {
      ...(placement.variant ? { variant: placement.variant } : {}),
      ...(type.integration !== 'core' ? { device: fitting.length === 1 ? fitting[0]!.id : null } : {}),
      ...(type.editor && type.opcodes ? { editor: { text: '' } } : {}),
    })
    const size = blockShape(ctx, block)
    const view = viewport ?? DEFAULT_VIEWPORT
    const spot = at ?? freeSpot(screenToCanvas(view, { x: surfaceSize.current.width / 2 - size.width / 2, y: surfaceSize.current.height / 3 }), size, rects())
    edit((p) => ({ ...p, program: { ...p.program, blocks: [...p.program.blocks, block] }, canvas: { ...p.canvas, blocks: { ...p.canvas.blocks, [id]: spot } } }))
    select(id)
  }

  /** Changes a block (its id stays). Edits with the same `key` in quick succession (typing in a field) are one undo step. */
  const update = (id: string, change: (block: BlockInstance) => BlockInstance, key?: string) =>
    edit((p) => ({ ...p, program: { ...p.program, blocks: p.program.blocks.map((b) => (b.id === id ? { ...change(b), id } : b)) } }), key)

  /** Changes several blocks at once (a link between two of them): one undo step. */
  const updateMany = (change: (block: BlockInstance) => BlockInstance) =>
    edit((p) => ({ ...p, program: { ...p.program, blocks: p.program.blocks.map((b) => ({ ...change(b), id: b.id })) } }))

  const remove = (ids: readonly string[]) => {
    if (!ids.length) return
    edit((p) => ({ ...p, program: { ...p.program, blocks: removeBlocks(p.program.blocks, ids) }, canvas: withoutBlocks(p.canvas, ids) }))
    setSelectedIds((now) => now.filter((id) => !ids.includes(id)))
  }

  /** Moves blocks to their new corners: one undo step. */
  const moveTo = (places: Readonly<Record<string, Point>>) => setLayout((l) => ({ ...l, blocks: { ...l.blocks, ...places } }))
  const moveBy = (ids: readonly string[], by: Point) =>
    moveTo(Object.fromEntries(ids.map((id) => {
      const at = positionOf(layout, program.blocks, id)
      return [id, snapPoint({ x: at.x + by.x, y: at.y + by.y })]
    })))

  const copy = () => {
    const found = selectedIds.flatMap((id) => program.blocks.find((b) => b.id === id) ?? [])
    if (!found.length) return
    setClipboard(found)
    pasted.current = 0
  }
  /** The copied blocks again, each paste two cells further down and right; the copies are selected. */
  const paste = () => {
    if (!clipboard.length) return
    pasted.current += 1
    const step = 2 * GRID * pasted.current
    const { added, ids } = copyBlocks(program.blocks, clipboard)
    const places = Object.fromEntries(clipboard.map((b) => {
      const at = positionOf(layout, program.blocks, b.id)
      return [ids.get(b.id)!, snapPoint({ x: at.x + step, y: at.y + step })]
    }))
    edit((p) => ({ ...p, program: { ...p.program, blocks: [...p.program.blocks, ...added] }, canvas: { ...p.canvas, blocks: { ...p.canvas.blocks, ...places } } }))
    selectMany(added.map((b) => b.id))
  }

  /* ---- free chips: a variable pulled off its pin onto the canvas ---- */

  const addChip = (path: ObjectPath, at: Point) => setLayout((l) => (chipFor(l, path) ? l : { ...l, chips: [...l.chips, { id: newChipId(l), path, ...at }] }))
  const moveChip = (id: string, at: Point) => setLayout((l) => ({ ...l, chips: l.chips.map((c) => (c.id === id ? { ...c, ...at } : c)) }))
  const removeChip = (id: string) => setLayout((l) => ({ ...l, chips: l.chips.filter((c) => c.id !== id) }))

  return {
    engine, catalog, ctx, program, layout, compiled, devices,
    selectedIds, selected, select, toggle, selectMany,
    viewport, setViewport, surfaceSize, paletteDrag,
    armed, arm: setArmed,
    selecting, setSelecting,
    add, update, updateMany, remove, moveTo, moveBy,
    copy, paste, canPaste: clipboard.length > 0,
    addChip, moveChip, removeChip,
    /** What the user calls a block. */
    nameOf: (id: string) => blockName(catalog, program.blocks, id),
    /** A change of the project and the canvas together: one undo step. */
    edit,
  }
}

export type BlocksState = ReturnType<typeof useBlocks>
