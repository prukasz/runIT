import { arrangeProgram } from '../../domain/canvas'
import { compileProgram } from '../../domain/compiler'
import type { Diagnostic } from '../../domain/compiler'
import { blockPinAt, DEFAULT_ENO, enumAlias, runitDeviceCatalog } from '../../domain/descriptors'
import type { DeviceCatalog, VmBlockPin, VmBlockPins, VmBlockType, VmCatalog, VmTemplate, VmTemplatePart } from '../../domain/descriptors'
import { GRID } from '../../domain/canvas'
import { isDynamicInput } from '../../domain/project/blockPins'
import { allDevices, defaultInstall, pinDisplayLabel, pinsOf, resolveDeviceValue } from '../../domain/devices'
import type { DeviceValue } from '../../domain/devices'
import { decompileExpression, expressionLanguage, tokenize } from '../../domain/expression'
import type { ObjectPath, ProgramBlock, ProjectCanvas, ProjectDevice, ProjectDocument } from '../../domain/project'

/*
 * Grid-sized function blocks: compact pins, or expanded source labels and
 * settings. The compiler remains the source of block diagnostics.
 */

/** The narrowest a block gets, whatever its content. */
const MIN_WIDTH = 6 * GRID
const HEADER = 2 * GRID
const ROW = GRID

export interface BlockPinView {
  readonly index: number
  /** The setting this input reads while unwired (`@overrides`): the detailed view draws it as a constant. */
  readonly overrides?: string
  readonly title: string
  readonly value: string
  readonly required: boolean
  /** What the chosen device's settings say the pin carries (`@spec`): `-45 to 45 deg`, drawn beside the title. */
  readonly hint?: string
  /** The title is not drawn: a variable is wired and its chip names the pin (`hide_when_wired`). */
  readonly quiet?: boolean
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
  if (field.anyValue !== undefined && (block.settings?.[name] === undefined || value === field.anyValue)) return long ? 'Any' : 'any'
  if (field.idKind === 'device') {
    if (block.settings?.[name] === undefined) return long ? 'No device' : 'no device'
    const device = allDevices(ctx.deviceCatalog ?? runitDeviceCatalog(), ctx.devices).find((entry) => entry.deviceId === value)
    return long ? `${device?.name ?? 'Unknown'} (#${value})` : device?.name ?? `#${value}`
  }
  if (field.idKind === 'pin') {
    const mask = type.fields.find((entry) => entry.letUserSelectAvailable === field.name)
    const dynamic = mask?.dynamicInput !== undefined && isDynamicInput(block, mask.dynamicInput)
    const deviceId = block.settings?.[field.deviceField!]
    if (deviceId === undefined && block.settings?.[name] === undefined) return 'no pin'
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
  const id = block.settings?.[field.name]
  if (field.anyValue !== undefined && (id === undefined || id === field.anyValue)) return 'Any device'
  if (id === undefined) return 'No device'
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

/** Width the pin rows need: per row the input's slab (type icon, name, padding) beside the output's, which overlap the block edge by 7 px each, and a gap between them. */
const pinsWidth = (inputs: readonly BlockPinView[], outputs: readonly BlockPinView[], deviceText: number): number => {
  const family = typeof document === 'undefined' ? 'sans-serif' : getComputedStyle(document.body).fontFamily
  const slab = (pin: BlockPinView | undefined): number => pin ? 35 + (pin.quiet ? 0 : 6 + textWidth(pin.title, `600 10.5px ${family}`, 6)) + (pin.hint ? 6 + textWidth(pin.hint, `10px ${family}`, 5.4) : 0) + 7 + 2 - 7 : 0
  let widest = 0
  for (let row = 0; row < Math.max(inputs.length, outputs.length); row++) widest = Math.max(widest, slab(inputs[row]) + slab(outputs[row]))
  // The device line (`Servo (#14)`) is drawn over the empty side of the pin rows, so it needs its own room beside the pins.
  if (deviceText && !(inputs.length && outputs.length)) widest += deviceText + 20
  return widest + (inputs.length && outputs.length ? 12 : 0)
}

/** The widest a block grows for its formula: a longer one wraps onto the summary's second line. */
const MAX_FORMULA_WIDTH = 28 * GRID

/** Width the detailed face's formula line needs (`Result = angle + 1`): its text in the summary's font, a chip's padding for the output and each input, the gaps between its parts, and the summary's side padding. */
const formulaWidth = (type: VmBlockType, block: FaceBlock): number => {
  const mono = '11px ui-monospace, SFMono-Regular, Consolas, monospace'
  const summary = (text: string) => textWidth(text, mono, 6.6) + 24
  const code = block.expression?.code
  if (!code?.length) return summary('Set a formula')
  const language = expressionLanguage(type.encoding!)
  const raw = decompileExpression(code, block.expression?.constants ?? [], language)
  if (!raw) return summary('Invalid formula')
  const outPath = block.outputs?.[0]
  let text = `${block.outputAliases?.[0] || (outPath ? pathText({ root: outPath }) : type.outputs.pins[0]?.title ?? 'OUT')}=`
  let chips = 1
  let parts = 2
  let at = 0
  for (const token of tokenize(raw, language.flavor).tokens) {
    if (token.start > at) { text += raw.slice(at, token.start); parts++ }
    if (token.kind === 'input') {
      const path = block.inputs?.[token.value!]
      text += path && path.root !== '' ? pathText(path) : `IN${token.value}`
      chips++
    } else text += token.text
    parts++
    at = token.end
  }
  if (at < raw.length) { text += raw.slice(at); parts++ }
  return summary(text) + chips * 12 + parts * 5
}

/** Whether the block is drawn in its detailed view: the toolbar's choice; some blocks are always detailed. */
export const isDetailed = (type: VmBlockType | undefined, toolbar: boolean): boolean =>
  !!type?.hasDetail && (type.alwaysDetailed || toolbar)

/** What a pin carries on the device the block works on (`@spec`): the variant its settings pick, with title, unit and range. Undefined without a spec or a chosen device. */
export const pinValueOf = (block: FaceBlock, pin: VmBlockPin | undefined, devices: readonly ProjectDevice[], deviceCatalog: DeviceCatalog = runitDeviceCatalog()): DeviceValue | undefined => {
  if (!pin?.valueSpec || !pin.deviceField) return undefined
  const id = block.settings?.[pin.deviceField]
  const device = typeof id === 'number' ? devices.find((entry) => entry.deviceId === id) : undefined
  const type = device && deviceCatalog.type(device.type)
  return type && device ? resolveDeviceValue(type, { ...defaultInstall(type), ...device.install }, pin.valueSpec) : undefined
}

export const blockShape = (type: VmBlockType | undefined, block: FaceBlock, detailed = false, devices: readonly ProjectDevice[] = [], deviceCatalog?: DeviceCatalog): BlockShape => {
  const expanded = isDetailed(type, detailed)
  const inputs = type ? pinViews(type.inputs, pinCount(type.inputs, block.inputs?.length ?? 0)).map((pin) => blockPinAt(type.inputs, pin.index)?.hideWhenWired && block.inputs?.[pin.index]?.root ? { ...pin, quiet: true } : pin).map((pin) => {
    const value = pinValueOf(block, blockPinAt(type.inputs, pin.index), devices, deviceCatalog)
    return value ? { ...pin, title: value.title, ...(value.hint ? { hint: value.hint } : {}) } : pin
  }).filter((pin) => !blockPinAt(type.inputs, pin.index)?.hiddenByDefault || block.dynamicInputs?.includes(pin.index) || block.inputs?.[pin.index] || (expanded && blockPinAt(type.inputs, pin.index)?.overrides)) : []
  const outputs = type ? pinViews(type.outputs, pinCount(type.outputs, block.outputs?.length ?? 0)) : []
  const rows = Math.max(1, inputs.length, outputs.length)
  const deviceLine = blockDeviceLine(type, block, devices, deviceCatalog)
  const family = typeof document === 'undefined' ? 'sans-serif' : getComputedStyle(document.body).fontFamily
  const deviceText = deviceLine ? textWidth(deviceLine, `10.5px ${family}`, 5.8) : 0
  const deviceWidth = deviceLine ? deviceText + 24 : 0
  const contentWidth = Math.max(headerWidth(type, block, devices, deviceCatalog), deviceWidth, pinsWidth(inputs, outputs, deviceText))
  // The block is as wide as its content needs (title, pin rows, device line, formula), on the grid: nothing is padded to a fixed width.
  const formulaNeeds = expanded && type?.encoding ? formulaWidth(type, block) : 0
  const formula = Math.min(MAX_FORMULA_WIDTH, formulaNeeds)
  // The summary is one line tall (its 8 px padding on both sides plus the line), one more for each time the formula wraps at the limit.
  const summaryHeight = expanded && type?.encoding ? Math.max(1, Math.ceil(formulaNeeds / MAX_FORMULA_WIDTH)) * ROW + 16 : 0
  const width = Math.max(MIN_WIDTH, Math.ceil(Math.max(contentWidth, formula) / GRID) * GRID)
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
