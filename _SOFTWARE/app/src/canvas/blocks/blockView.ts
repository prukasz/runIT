import { arrangeProgram } from '../../domain/canvas'
import { compileProgram } from '../../domain/compiler'
import type { Diagnostic } from '../../domain/compiler'
import { blockPinAt, enumAlias, runitDeviceCatalog } from '../../domain/descriptors'
import type { DeviceCatalog, VmBlockPins, VmBlockType, VmCatalog } from '../../domain/descriptors'
import { GRID } from '../../domain/canvas'
import { decompileExpression, expressionLanguage } from '../../domain/expression'
import { isDynamicInput } from '../../domain/project/blockPins'
import { allDevices, pinsOf } from '../../domain/devices'
import type { ObjectPath, ProgramBlock, ProjectCanvas, ProjectDevice, ProjectDocument } from '../../domain/project'

/*
 * Grid-sized function blocks: compact pins, or expanded source labels and
 * settings. The compiler remains the source of block diagnostics.
 */

export const BLOCK_WIDTH = 10 * GRID
const HEADER = 2 * GRID
const ROW = GRID

export interface BlockPinView {
  readonly index: number
  readonly title: string
  readonly value: string
  readonly required: boolean
}

export interface BlockShape {
  readonly width: number
  readonly height: number
  readonly inputs: readonly BlockPinView[]
  readonly outputs: readonly BlockPinView[]
  /** Pin row height (doubled when expanded). */
  readonly row: number
}

/** Editor settings only; never present private runtime state as a live value. */
export const blockSummary = (type: VmBlockType | undefined, block: ProgramBlock, devices: readonly ProjectDevice[] = [], deviceCatalog: DeviceCatalog = runitDeviceCatalog()): readonly string[] => {
  if (!type) return []
  if (type.encoding) {
    if (!block.expression?.code.length) return ['Set a formula']
    return [decompileExpression(block.expression.code, block.expression.constants ?? [], expressionLanguage(type.encoding)) ?? 'Invalid formula']
  }
  const summary = type.fields.filter((field) => field.source === 'user' && !field.flexible && field.extendedViewShow).map((field) => {
    const value = block.settings?.[field.name] ?? 0
    if (field.idKind === 'device') {
      const device = allDevices(deviceCatalog, devices).find((entry) => entry.deviceId === value)
      return `Device: ${device?.name ?? 'Unknown'} (#${value})`
    }
    if (field.idKind === 'pin') {
      const deviceId = block.settings?.[field.deviceField!] ?? 0
      const device = allDevices(deviceCatalog, devices).find((entry) => entry.deviceId === deviceId)
      const pin = pinsOf(device?.type).find((entry) => entry.value === value)
      const label = pin && pin.label !== String(value) ? `${pin.label} (${value})` : String(value)
      const mask = type.fields.find((entry) => entry.letUserSelectAvailable === field.name)
      return mask?.dynamicInput !== undefined && isDynamicInput(block, mask.dynamicInput) ? `Pin: Dynamic (default ${label})` : `Pin: ${label}`
    }
    const members = field.enumRef ? type.enums.get(field.enumRef) : undefined
    const member = members?.find((entry) => entry.value === value || entry.name === value || entry.name.endsWith(`_${value}`))
    const label = member ? enumAlias(member.name.replace(/^VM_[A-Z]+_(?:UNIT_)?/, '')) : String(value)
    return `${field.name.replace(/^k_/, '').replace(/_/g, ' ')}: ${label}`
  })
  if (type.fields.some((field) => field.source === 'derived' && field.cType === 'vm_span_t')) summary.push(`body: ${block.body ?? 0} blocks`)
  return summary
}

/** A setting the way the block shows it: an enum member's short name, else the number. */
const settingText = (type: VmBlockType, block: ProgramBlock, name: string): string => {
  const field = type.fields.find((entry) => entry.name === name)
  const value = block.settings?.[name] ?? 0
  const member = field?.enumRef ? type.enums.get(field.enumRef)?.find((entry) => entry.value === value || entry.name === value || entry.name.endsWith(`_${value}`)) : undefined
  return member ? enumAlias(member.name.replace(/^VM_[A-Z]+_(?:UNIT_)?/, '')) : String(value)
}

/** The block face's title in two parts: the block's words, then the value part it is set to (`Every` | `100 MS`, `Toggle Pin` | `#22`), which the face draws apart. */
export interface BlockHeadline {
  readonly lead: string
  readonly value?: string
}

/** The pin an IO block drives: `#22`, or `dynamic` while the Pin input picks it at run time. */
const pinText = (type: VmBlockType, block: ProgramBlock): string => {
  const mask = type.fields.find((entry) => entry.letUserSelectAvailable === 'default_io_num')
  return mask?.dynamicInput !== undefined && isDynamicInput(block, mask.dynamicInput) ? 'dynamic pin' : `#${block.settings?.default_io_num ?? 0}`
}

/** The block's title with its main settings, for the block face: `Every 100 MS`. Nothing to add for a block without settings worth a glance. */
export const blockHeadlineParts = (type: VmBlockType | undefined, block: ProgramBlock, labelOf: (path: ObjectPath) => string = pathText): BlockHeadline | undefined => {
  if (!type) return undefined
  const text = (name: string) => settingText(type, block, name)
  // A setting whose input pin is wired reads from there: the face names what feeds it, not the unused constant.
  const wired = (name: string, pin: number) => {
    const path = block.inputs?.[pin]
    return path && path.root !== '' ? labelOf(path) : text(name)
  }
  switch (type.key) {
    case 'PERIODIC': return { lead: 'Every', value: `${wired('period', 0)} ${text('time_base')}` }
    case 'TIMER': return { lead: 'Timer', value: `${text('mode')} ${wired('pt', 1)} ${text('time_base')}` }
    case 'EDGE': return { lead: `${text('edge_type')} edge, change ${wired('change_by', 1)}` }
    case 'LATCH': return { lead: `Latch ${text('mode')}` }
    case 'FOR': return { lead: 'For', value: `${wired('k_start', 0)} to ${wired('k_end', 1)} step ${wired('k_step', 2)}` }
    case 'IO_SET_LEVEL':
    case 'IO_TOGGLE': return { lead: type.title, value: pinText(type, block) }
    default: return undefined
  }
}

/** `blockHeadlineParts` as one line: `Every 100 MS`. */
export const blockHeadline = (type: VmBlockType | undefined, block: ProgramBlock, labelOf?: (path: ObjectPath) => string): string | undefined => {
  const parts = blockHeadlineParts(type, block, labelOf)
  return parts && (parts.value ? `${parts.lead} ${parts.value}` : parts.lead)
}

/** The device an IO block works on, for the face's second line: `GPIO_ESP (#0)`. Nothing for a block without a device. */
export const blockDeviceLine = (type: VmBlockType | undefined, block: ProgramBlock, devices: readonly ProjectDevice[] = [], deviceCatalog: DeviceCatalog = runitDeviceCatalog()): string | undefined => {
  const field = type?.fields.find((entry) => entry.source === 'user' && entry.idKind === 'device')
  if (!field) return undefined
  const id = block.settings?.[field.name] ?? 0
  const device = allDevices(deviceCatalog, devices).find((entry) => entry.deviceId === id)
  return `${device?.name ?? 'Unknown device'} (#${id})`
}

/** Pins the block has: as many as wired or chosen, at least the minimum and every listed pin, at most the maximum (like the compiler). */
export const pinCount = (pins: VmBlockPins, used: number): number => Math.min(pins.max, Math.max(used, pins.min, pins.pins.length))

const pinViews = (pins: VmBlockPins, count: number): BlockPinView[] =>
  Array.from({ length: count }, (_, index) => {
    const pin = blockPinAt(pins, index)!
    // A repeating pin (EXPR's inputs, SWITCH's branches) is numbered.
    const repeated = index >= pins.pins.length - 1 && pins.max > pins.pins.length
    return { index, title: repeated ? `${pin.title} ${index}` : pin.title, value: pin.value, required: pin.required }
  })

export const blockShape = (type: VmBlockType | undefined, block: ProgramBlock & { readonly view?: 'simple' | 'detailed' }, detailed = false): BlockShape => {
  const expanded = !type?.simpleOnly && (block.view ? block.view === 'detailed' : detailed)
  const inputs = type ? pinViews(type.inputs, pinCount(type.inputs, block.inputs?.length ?? 0)).filter((pin) => !blockPinAt(type.inputs, pin.index)?.hiddenByDefault || block.dynamicInputs?.includes(pin.index) || block.inputs?.[pin.index]) : []
  const outputs = type ? pinViews(type.outputs, pinCount(type.outputs, block.outputs?.length ?? 0)) : []
  const rows = Math.max(1, inputs.length, outputs.length)
  const summaryHeight = expanded ? (type?.encoding ? 2 * ROW + 16 : Math.max(1, blockSummary(type, block).length) * ROW + 16) : 0
  return { width: BLOCK_WIDTH + (expanded ? 4 * GRID : 0), height: HEADER + rows * ROW + summaryHeight, inputs, outputs, row: ROW }
}

/** Where a wire meets a block, in canvas units: input pins on the left edge, outputs on the right, EN / ENO at header height. */
export const pinAnchor = (block: { readonly x: number; readonly y: number }, shape: BlockShape, end: { side: 'in' | 'out'; index: number } | 'en' | 'eno'): { x: number; y: number } => {
  if (end === 'en') return { x: block.x, y: block.y + HEADER / 2 }
  if (end === 'eno') return { x: block.x + shape.width, y: block.y + HEADER / 2 }
  const rowOf = (row: number) => block.y + HEADER + row * shape.row + shape.row / 2
  const pins = end.side === 'in' ? shape.inputs : shape.outputs
  const row = Math.max(0, pins.findIndex((pin) => pin.index === end.index))
  return { x: end.side === 'in' ? block.x : block.x + shape.width, y: rowOf(row) }
}

/** A path as text: `motor.gains[2]`, `table[sel]`, `periodic1:q0`. */
export const pathText = (path: ObjectPath): string =>
  path.root + (path.steps ?? []).map((step) => (step.kind === 'index' ? `[${step.index}]` : step.kind === 'name' ? `.${step.name}` : `[${pathText(step.index)}]`)).join('')

/** Block ID of a diagnostic: its block, or the block whose pin path it is about (`block:<id>:in0`). */
const blockOf = (diagnostic: Diagnostic): string | undefined => diagnostic.blockId ?? diagnostic.pathKey?.match(/^block:(.+):(?:in|en)\d+$/)?.[1]

/**
 * The compiler's findings per block, for every canvas (a disabled one too, so
 * it can be fixed before it is enabled). Program-wide findings are under ''.
 */
export const blockDiagnostics = (project: ProjectDocument, canvases: readonly ProjectCanvas[], catalog: VmCatalog, maxFrameBytes: number): ReadonlyMap<string, readonly Diagnostic[]> => {
  const byBlock = new Map<string, Diagnostic[]>()
  if (!canvases.some((canvas) => canvas.blocks.length)) return byBlock
  const arranged = arrangeProgram(canvases, { includeDisabled: true })
  const compiled = compileProgram(project, catalog, { maxFrameBytes, blocks: arranged.blocks })
  for (const diagnostic of [...arranged.diagnostics, ...compiled.diagnostics]) {
    const key = blockOf(diagnostic) ?? ''
    byBlock.set(key, [...(byBlock.get(key) ?? []), diagnostic])
  }
  return byBlock
}
