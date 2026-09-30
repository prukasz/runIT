import { arrangeProgram } from '../../domain/canvas'
import { compileProgram } from '../../domain/compiler'
import type { Diagnostic } from '../../domain/compiler'
import { blockPinAt, DEFAULT_ENO, enumAlias, runitDeviceCatalog } from '../../domain/descriptors'
import type { DeviceCatalog, VmBlockPins, VmBlockType, VmCatalog, VmTemplate, VmTemplatePart } from '../../domain/descriptors'
import { GRID } from '../../domain/canvas'
import { isDynamicInput } from '../../domain/project/blockPins'
import { allDevices, pinDisplayLabel, pinsOf } from '../../domain/devices'
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
  /** The setting this input reads while unwired (`@overrides`): the detailed view draws it as a constant. */
  readonly overrides?: string
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

/*
 * A block's face is described by its generated descriptor (`//@header`,
 * `//@body`, `@overrides` in the firmware header): lines of text with
 * references the app resolves here. Nothing below knows a block type.
 */

/** A block as placed on a canvas: its output pins may carry names of their own. */
type FaceBlock = ProgramBlock & { readonly outputAliases?: readonly (string | null)[]; readonly name?: string }

interface FaceContext {
  readonly type: VmBlockType
  readonly block: FaceBlock
  readonly labelOf: (path: ObjectPath) => string
  readonly devices: readonly ProjectDevice[]
  readonly deviceCatalog?: DeviceCatalog
}

const isWired = (path: ObjectPath | null | undefined): path is ObjectPath => !!path && path.root !== ''

/** An enum member as a face words it: its `@alias` (`<`), else the name without its prefix (`SET_DOMINANT` -> `Set dominant`). */
const enumText = (type: VmBlockType, enumName: string, value: number | string): string => {
  const member = type.enums.get(enumName)?.find((entry) => entry.value === value || entry.name === value || entry.name.endsWith(`_${value}`))
  return member ? member.alias ?? enumAlias(member.name.replace(/^VM_[A-Z]+_(?:UNIT_)?/, '')) : String(value)
}

/** A setting the way the face shows it. `long` adds what identifies it (`GPIO_ESP (#0)`, `GPIO4 (4)`); short is for the line under the title (`#4`). */
const fieldText = (ctx: FaceContext, name: string, long: boolean): string => {
  const { type, block } = ctx
  const field = type.fields.find((entry) => entry.name === name)
  const value = block.settings?.[name] ?? 0
  if (!field) return String(value)
  if (field.idKind === 'device') {
    const device = allDevices(ctx.deviceCatalog ?? runitDeviceCatalog(), ctx.devices).find((entry) => entry.deviceId === value)
    return long ? `${device?.name ?? 'Unknown'} (#${value})` : device?.name ?? `#${value}`
  }
  if (field.idKind === 'pin') {
    const mask = type.fields.find((entry) => entry.letUserSelectAvailable === field.name)
    const dynamic = mask?.dynamicInput !== undefined && isDynamicInput(block, mask.dynamicInput)
    const deviceId = block.settings?.[field.deviceField!] ?? 0
    const catalog = ctx.deviceCatalog ?? runitDeviceCatalog()
    const device = allDevices(catalog, ctx.devices).find((entry) => entry.deviceId === deviceId)
    const alias = device && catalog.pinAliases?.[device.ref]?.[String(value)]
    if (!long) return dynamic ? 'dynamic pin' : alias ? `${alias} (#${value})` : `#${value}`
    const pin = pinsOf(device?.type).find((entry) => entry.value === value)
    const label = alias ? pinDisplayLabel(catalog, device?.ref, Number(value), pin?.label) : pin && pin.label !== String(value) ? `${pin.label} (${value})` : String(value)
    return dynamic ? `Dynamic (default ${label})` : label
  }
  return field.enumRef ? enumText(type, field.enumRef, value) : String(value)
}

/** One reference of a template: an input pin reads from what feeds it, else from the constant it overrides. */
const refText = (ctx: FaceContext, part: Extract<VmTemplatePart, { ref: string }>, long: boolean): string => {
  const { type, block } = ctx
  switch (part.kind) {
    case 'title': return type.title
    case 'field': return fieldText(ctx, part.field!, long)
    case 'out': {
      const target = block.outputs?.[part.pin!]
      return block.outputAliases?.[part.pin!] || (target ? ctx.labelOf({ root: target }) : part.ref)
    }
    case 'pin': {
      // A wired pin names its source; one the user made dynamic says so; otherwise the constant it overrides.
      const path = block.inputs?.[part.pin!]
      if (isWired(path)) return ctx.labelOf(path)
      if (block.dynamicInputs?.includes(part.pin!)) return type.fields.some((entry) => entry.idKind === 'pin' && entry.name === part.field) ? 'dynamic pin' : 'dynamic'
      return part.field ? fieldText(ctx, part.field, long) : blockPinAt(type.inputs, part.pin!)?.title ?? part.ref
    }
    default: return part.ref
  }
}

const templateText = (ctx: FaceContext, template: VmTemplate, long: boolean): string =>
  template.map((part) => ('text' in part ? part.text : refText(ctx, part, long))).join('')

/** A setting as the face words it, short: `+`, `#4`, `300`. */
export const settingText = (type: VmBlockType, block: FaceBlock, name: string, devices: readonly ProjectDevice[] = [], deviceCatalog?: DeviceCatalog): string =>
  fieldText({ type, block, labelOf: pathText, devices, deviceCatalog }, name, false)

/** The block face's title in two parts: the block's words, then the value part it is set to (`Every` | `100 MS`, `Toggle Pin` | `#22`), which the face draws apart. */
export interface BlockHeadline {
  readonly lead: string
  readonly value?: string
}

/** The block's `//@header` for this block: its words with the settings (or what feeds them) filled in, `Every 100 MS`. Nothing for a block without a header. */
export const blockHeadlineParts = (type: VmBlockType | undefined, block: FaceBlock, labelOf: (path: ObjectPath) => string = pathText, devices: readonly ProjectDevice[] = [], deviceCatalog?: DeviceCatalog): BlockHeadline | undefined => {
  if (!type?.header) return undefined
  const ctx: FaceContext = { type, block, labelOf, devices, deviceCatalog }
  const lead = templateText(ctx, type.header.lead, false)
  return type.header.value ? { lead, value: templateText(ctx, type.header.value, false) } : { lead }
}

/** `blockHeadlineParts` as one line: `Every 100 MS`. */
export const blockHeadline = (type: VmBlockType | undefined, block: FaceBlock, labelOf?: (path: ObjectPath) => string, devices: readonly ProjectDevice[] = [], deviceCatalog?: DeviceCatalog): string | undefined => {
  const parts = blockHeadlineParts(type, block, labelOf, devices, deviceCatalog)
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
    return { index, title: repeated ? `${pin.title} ${index}` : pin.title, value: pin.value, required: pin.required, ...(pin.overrides ? { overrides: pin.overrides } : {}) }
  })

/** The headline as the small line under the title says it: without the block's own title when the face already has it (`For 0 to 3` under the name `For` reads `0 to 3`). */
export const blockSubtitleHeadline = (type: VmBlockType | undefined, block: FaceBlock, labelOf?: (path: ObjectPath) => string, devices: readonly ProjectDevice[] = [], deviceCatalog?: DeviceCatalog): string | undefined => {
  const parts = blockHeadlineParts(type, block, labelOf, devices, deviceCatalog)
  return parts?.value !== undefined && parts.lead === type?.title ? parts.value : blockHeadline(type, block, labelOf, devices, deviceCatalog)
}

let measuring: CanvasRenderingContext2D | null | undefined

/** Width of `text` in `font`, measured in the browser; an estimate where there is no canvas (server rendering, tests). */
const textWidth = (text: string, font: string, perChar: number): number => {
  if (measuring === undefined) {
    try {
      measuring = typeof document === 'undefined' ? null : document.createElement('canvas').getContext('2d')
    } catch {
      measuring = null
    }
  }
  if (!measuring) return text.length * perChar
  measuring.font = font
  return measuring.measureText(text).width
}


const BADGE = 24

/** Width the block's title needs, including its value beside a custom name and any ID shown in detailed view. */
const headerWidth = (type: VmBlockType | undefined, block: FaceBlock, devices: readonly ProjectDevice[], deviceCatalog?: DeviceCatalog): number => {
  if (!type) return 0
  const family = typeof document === 'undefined' ? 'sans-serif' : getComputedStyle(document.body).fontFamily
  const namedValue = block.name ? blockSubtitleHeadline(type, block, pathText, devices, deviceCatalog) : undefined
  const title = block.name ? [block.name, namedValue].filter(Boolean).join(' ') : blockHeadline(type, block, pathText, devices, deviceCatalog) || type.title
  const eno = type.eno.title !== DEFAULT_ENO.title ? textWidth(type.eno.title, `600 10px ${family}`, 6.2) + 6 : 0
  // The title keeps its room beside the ENO name and the problem-count badge, which shares the header.
  return textWidth(title, `600 12px ${family}`, 6.8) + eno + BADGE + 24 + 8
}

/** Whether the block is drawn in its detailed view: the block's own choice, else the toolbar; some blocks are always detailed. */
export const isDetailed = (type: VmBlockType | undefined, block: { readonly view?: 'simple' | 'detailed' }, toolbar: boolean): boolean =>
  !!type?.hasDetail && (type.alwaysDetailed || (block.view ? block.view === 'detailed' : toolbar))

export const blockShape = (type: VmBlockType | undefined, block: FaceBlock & { readonly view?: 'simple' | 'detailed' }, detailed = false, devices: readonly ProjectDevice[] = [], deviceCatalog?: DeviceCatalog): BlockShape => {
  const expanded = isDetailed(type, block, detailed)
  const inputs = type ? pinViews(type.inputs, pinCount(type.inputs, block.inputs?.length ?? 0)).filter((pin) => !blockPinAt(type.inputs, pin.index)?.hiddenByDefault || block.dynamicInputs?.includes(pin.index) || block.inputs?.[pin.index] || (expanded && blockPinAt(type.inputs, pin.index)?.overrides)) : []
  const outputs = type ? pinViews(type.outputs, pinCount(type.outputs, block.outputs?.length ?? 0)) : []
  const rows = Math.max(1, inputs.length, outputs.length)
  const summaryHeight = expanded && type?.encoding ? 2 * ROW + 16 : 0
  // Blocks without visible pins need room for their text, not the two pin columns.
  const minimum = inputs.length || outputs.length ? BLOCK_WIDTH : 6 * GRID
  const deviceLine = blockDeviceLine(type, block, devices, deviceCatalog)
  const family = typeof document === 'undefined' ? 'sans-serif' : getComputedStyle(document.body).fontFamily
  const deviceWidth = deviceLine ? textWidth(deviceLine, `10.5px ${family}`, 5.8) + 24 : 0
  const contentWidth = Math.max(headerWidth(type, block, devices, deviceCatalog), deviceWidth)
  const width = Math.max(minimum + (expanded && type?.encoding ? 4 * GRID : 0), Math.ceil(contentWidth / GRID) * GRID)
  return { width, height: HEADER + rows * ROW + summaryHeight, inputs, outputs, row: ROW }
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
