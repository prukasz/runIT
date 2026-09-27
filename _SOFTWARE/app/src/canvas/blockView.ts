import { compileProgram } from '../domain/compiler'
import type { Diagnostic } from '../domain/compiler'
import { blockPinAt, runitDeviceCatalog } from '../domain/descriptors'
import type { DeviceCatalog, VmBlockPins, VmBlockType, VmCatalog } from '../domain/descriptors'
import { GRID } from '../domain/canvas'
import { decompileExpression, expressionLanguage } from '../domain/expression'
import { isDynamicInput } from '../domain/project/blockPins'
import { allDevices, pinsOf } from '../domain/devices'
import type { ObjectPath, ProgramBlock, ProjectCanvas, ProjectDevice, ProjectDocument } from '../domain/project'

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
    const label = member ? member.name.replace(/^VM_[A-Z]+_(?:UNIT_)?/, '') : String(value)
    return `${field.name.replace(/^k_/, '').replace(/_/g, ' ')}: ${label}`
  })
  if (type.fields.some((field) => field.source === 'derived' && field.cType === 'vm_span_t')) summary.push(`body: ${block.body ?? 0} blocks`)
  return summary
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
  const expanded = block.view ? block.view === 'detailed' : detailed
  const inputs = type ? pinViews(type.inputs, pinCount(type.inputs, block.inputs?.length ?? 0)).filter((pin) => !blockPinAt(type.inputs, pin.index)?.hiddenByDefault || block.dynamicInputs?.includes(pin.index) || block.inputs?.[pin.index]) : []
  const outputs = type ? pinViews(type.outputs, pinCount(type.outputs, block.outputs?.length ?? 0)) : []
  const rows = Math.max(1, inputs.length, outputs.length)
  return { width: BLOCK_WIDTH + (expanded ? 4 * GRID : 0), height: HEADER + rows * ROW + (expanded ? rows * ROW + Math.max(1, blockSummary(type, block).length) * ROW + GRID : 0), inputs, outputs }
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
  const blocks = canvases.flatMap((canvas) => canvas.blocks.map(({ x: _x, y: _y, view: _view, ...block }) => block))
  const byBlock = new Map<string, Diagnostic[]>()
  if (!blocks.length) return byBlock
  const compiled = compileProgram(project, catalog, { maxFrameBytes, blocks })
  for (const diagnostic of compiled.diagnostics) {
    const key = blockOf(diagnostic) ?? ''
    byBlock.set(key, [...(byBlock.get(key) ?? []), diagnostic])
  }
  return byBlock
}
