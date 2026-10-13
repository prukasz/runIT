import { BLOCKS } from '../backend/vm/blocks'
import type { BlockDescriptor, BlockOp, BlockPin, BlockPins, FacePart, OpPin } from '../backend/vm/blocks'
import { GRID } from '../domain/canvas'
import type { ObjectPath, ProgramBlock } from '../domain/canvas'

/*
 * What a block looks like on a canvas, from its generated descriptor: the headline from the descriptor's face, the pins from its
 * input and output lists, and the size, the least that holds them (on the grid).
 */

/** A block's type key: the descriptor's name without its prefix (`EXPR`). */
export const typeKeyOf = (descriptor: BlockDescriptor): string => descriptor.name.replace(/^VM_BLK_/, '')

export const descriptorOf = (typeKey: string): BlockDescriptor | undefined => [...BLOCKS.values()].find((entry) => typeKeyOf(entry) === typeKey)

/** The operation a device block is set to (its `op` setting), if the descriptor has operations and one is chosen. */
export const opOf = (descriptor: BlockDescriptor | undefined, block: ProgramBlock): BlockOp | undefined => {
  if (!descriptor?.ops) return undefined
  const value = block.settings?.[descriptor.op_field ?? 'op']
  return descriptor.ops.find((entry) => entry.value === value)
}

/** The value kind an operation's pin shows: a choice or a number. */
const opPinValue = (pin: OpPin): string => (pin.value_type ?? (pin.choices ? 'u8' : pin.field_type ?? 'number'))

const opPinViews = (pins: readonly OpPin[]): PinView[] =>
  pins.map((pin, index) => ({ index, title: pin.unit ? `${pin.alias} (${pin.unit})` : pin.alias, value: opPinValue(pin), required: false }))

const MIN_WIDTH = 6 * GRID
export const HEADER = 2 * GRID
const ROW = GRID

export interface PinView {
  readonly index: number
  readonly title: string
  /** The pin's value kind (`u32`, `f32`, `gate` ...). */
  readonly value: string
  readonly required: boolean
  /** The setting this input reads while unwired. */
  readonly overrides?: string
  /** The title is not drawn: a variable is wired and its chip names the pin. */
  readonly quiet?: boolean
}

export interface BlockShape {
  readonly width: number
  readonly height: number
  readonly inputs: readonly PinView[]
  readonly outputs: readonly PinView[]
  /** Pin row height. */
  readonly row: number
}

/** A path as text: `motor.gains[2]`, `periodic1:q0`. */
export const pathText = (path: ObjectPath): string =>
  path.root + (path.steps ?? []).map((step) => (step.kind === 'index' ? `[${step.index}]` : step.kind === 'name' ? `.${step.name}` : `[${pathText(step.index)}]`)).join('')

const pinAt = (pins: BlockPins, index: number): BlockPin | undefined => pins.pins[index] ?? pins.pins.at(-1)

/** A pin the descriptor does not list (a block wired past its listed pins, or one that lists none): a plain numbered number pin. */
const spare = (index: number, output: boolean): BlockPin => ({ index, name: `${output ? 'out' : 'in'}${index}`, alias: output ? 'Out' : 'In', value: 'number' })

/** Pins the block has: as many as wired, at least the minimum and every listed pin, at most the maximum. */
const pinCount = (pins: BlockPins, used: number): number => Math.min(pins.max, Math.max(used, pins.min, pins.pins.length))

const pinViews = (pins: BlockPins | undefined, used: number, output: boolean, wired: (index: number) => boolean): PinView[] => {
  if (!pins) return []
  return Array.from({ length: pinCount(pins, used) }, (_, index) => {
    const listed = pinAt(pins, index)
    const pin = listed ?? spare(index, output)
    // A repeating pin (EXPR's inputs, SWITCH's branches), and one the descriptor does not list, is numbered.
    const repeated = !listed || (index >= pins.pins.length - 1 && pins.max > pins.pins.length)
    return {
      index,
      title: repeated ? `${pin.alias} ${index}` : pin.alias,
      value: pin.value,
      required: !!pin.required,
      ...(pin.overrides ? { overrides: pin.overrides } : {}),
      ...(pin.hide_when_wired && wired(index) ? { quiet: true } : {}),
    }
  })
}

// ---------------------------------------------------------------------------
// The headline
// ---------------------------------------------------------------------------

export interface Headline {
  readonly lead: string
  readonly value?: string
}

const isWired = (path: ObjectPath | null | undefined): path is ObjectPath => !!path && path.root !== ''

/** A setting as the face words it: its value, or what says it is not set yet. */
const fieldText = (descriptor: BlockDescriptor, block: ProgramBlock, name: string): string => {
  const value = block.settings?.[name]
  if (value !== undefined) return String(value)
  return descriptor.state?.fields.find((field) => field.name === name)?.id_kind === 'device' ? 'no device' : '…'
}

const partText = (descriptor: BlockDescriptor, block: ProgramBlock, labelOf: (path: ObjectPath) => string, part: FacePart): string => {
  if ('text' in part) return part.text
  switch (part.kind) {
    case 'title': return descriptor.alias
    case 'field': return fieldText(descriptor, block, part.field!)
    // The operation of a device block (it decides the pins): its name once chosen, else the block's own.
    case 'op': return block.settings?.op === undefined ? descriptor.alias : String(block.settings.op)
    case 'out': {
      const target = block.outputs?.[part.pin!]
      return target ? labelOf({ root: target }) : part.ref
    }
    case 'pin': {
      const path = block.inputs?.[part.pin!]
      if (isWired(path)) return labelOf(path)
      if (part.field) return fieldText(descriptor, block, part.field)
      return pinAt(descriptor.inputs ?? { min: 0, max: 0, pins: [] }, part.pin!)?.alias ?? part.ref
    }
  }
}

/** A reference in an operation's face: the pin's source when it is wired, else its name. */
const opPartText = (op: BlockOp, block: ProgramBlock, labelOf: (path: ObjectPath) => string, part: Exclude<FacePart, { text: string }>): string => {
  const index = part.pin ?? op.inputs.findIndex((pin) => pin.name === part.ref)
  const path = block.inputs?.[index]
  if (isWired(path)) return labelOf(path)
  return op.inputs[index]?.alias ?? part.ref
}

/** The block's words with its settings (or what feeds them) filled in: `Every` | `100 MS`. Just the title for a block without a face. */
export const headlineOf = (descriptor: BlockDescriptor | undefined, block: ProgramBlock, labelOf: (path: ObjectPath) => string = pathText): Headline => {
  if (!descriptor) return { lead: `Unknown ${block.type}` }
  if (descriptor.ops) {
    // A device block: what it does (its operation's face, or the block's name until one is chosen), then the device.
    const op = opOf(descriptor, block)
    const device = fieldText(descriptor, block, descriptor.device_field ?? 'device_id')
    const lead = op?.face
      ? op.face.lead.map((part) => ('text' in part ? part.text : opPartText(op, block, labelOf, part))).join('')
      : op?.alias ?? descriptor.alias
    return { lead, value: device }
  }
  const face = descriptor.face
  if (!face) return { lead: descriptor.alias }
  const text = (parts: readonly FacePart[]) => parts.map((part) => partText(descriptor, block, labelOf, part)).join('')
  const lead = text(face.lead)
  return face.value ? { lead, value: text(face.value) } : { lead }
}

// ---------------------------------------------------------------------------
// The size
// ---------------------------------------------------------------------------

let measuring: CanvasRenderingContext2D | null | undefined

/** Width of `text` in `font`, measured in the browser; an estimate where there is no canvas. */
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

const family = (): string => (typeof document === 'undefined' ? 'sans-serif' : getComputedStyle(document.body).fontFamily)

/** Width the pin rows need: per row the input's slab (type icon, name, padding) beside the output's, and a gap between them. */
const pinsWidth = (inputs: readonly PinView[], outputs: readonly PinView[]): number => {
  const font = family()
  const slab = (pin: PinView | undefined): number => (pin ? 35 + (pin.quiet ? 0 : 6 + textWidth(pin.title, `600 10.5px ${font}`, 6)) + 7 + 2 - 7 : 0)
  let widest = 0
  for (let row = 0; row < Math.max(inputs.length, outputs.length); row++) widest = Math.max(widest, slab(inputs[row]) + slab(outputs[row]))
  return widest + (inputs.length && outputs.length ? 12 : 0)
}

/** The block's size, the least that holds what it shows, on the grid. */
export const blockShape = (descriptor: BlockDescriptor | undefined, block: ProgramBlock & { readonly name?: string }): BlockShape => {
  // A device block's pins come from its operation; until one is chosen it has none.
  const op = opOf(descriptor, block)
  const inputs = descriptor?.ops ? (op ? opPinViews(op.inputs) : []) : pinViews(descriptor?.inputs, block.inputs?.length ?? 0, false, (index) => isWired(block.inputs?.[index]))
  const outputs = descriptor?.ops ? (op ? opPinViews(op.outputs) : []) : pinViews(descriptor?.outputs, block.outputs?.length ?? 0, true, () => false)
  const rows = Math.max(1, inputs.length, outputs.length)
  const head = headlineOf(descriptor, block)
  const title = block.name ?? [head.lead, head.value].filter(Boolean).join(' ')
  const header = textWidth(title, `600 12px ${family()}`, 6.8) + 24 + 8
  const width = Math.max(MIN_WIDTH, Math.ceil(Math.max(header, pinsWidth(inputs, outputs)) / GRID) * GRID)
  return { width, height: HEADER + rows * ROW, inputs, outputs, row: ROW }
}

/** Where a wire meets a block, in canvas units: input pins on the left edge, outputs on the right, EN / ENO at header height. */
export const pinAnchor = (block: { readonly x: number; readonly y: number }, shape: BlockShape, end: { side: 'in' | 'out'; index: number } | 'en' | 'eno'): { x: number; y: number } => {
  if (end === 'en') return { x: block.x, y: block.y + HEADER / 2 }
  if (end === 'eno') return { x: block.x + shape.width, y: block.y + HEADER / 2 }
  const pins = end.side === 'in' ? shape.inputs : shape.outputs
  const row = Math.max(0, pins.findIndex((pin) => pin.index === end.index))
  return { x: end.side === 'in' ? block.x : block.x + shape.width, y: block.y + HEADER + row * shape.row + shape.row / 2 }
}

/** What a palette drag carries: a block's type key, and for a device block the value of the operation to start with (`SERVO#65282`). */
export const encodePlacement = (typeKey: string, op?: number): string => (op === undefined ? typeKey : `${typeKey}#${op}`)

export const decodePlacement = (text: string): { typeKey: string; settings?: Readonly<Record<string, number>> } => {
  const [typeKey = '', op] = text.split('#')
  const field = descriptorOf(typeKey)?.op_field ?? 'op'
  return op === undefined ? { typeKey } : { typeKey, settings: { [field]: Number(op) } }
}
