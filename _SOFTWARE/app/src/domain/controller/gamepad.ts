import type { ControllerWidget } from './model'

/*
 * An Xbox (standard layout) gamepad driving the widgets. By default nothing is set up: the widgets of a kind take
 * the pad's controls in the order they sit on the layout (top to bottom, then left to right), so the first joystick
 * is the left stick, the second the right stick, and so on. A widget can be given a control by hand instead (its
 * `pad`), or none. The remote shows each widget's control in the editor.
 */

export const PAD_SOURCES = ['leftStick', 'rightStick', 'leftStickX', 'leftStickY', 'rightStickX', 'rightStickY', 'dpad', 'leftTrigger', 'rightTrigger', 'a', 'b', 'x', 'y', 'lb', 'rb', 'back', 'start'] as const
export type PadSource = (typeof PAD_SOURCES)[number]
export const isPadSource = (value: unknown): value is PadSource => typeof value === 'string' && (PAD_SOURCES as readonly string[]).includes(value)

const BUTTONS: readonly PadSource[] = ['a', 'b', 'x', 'y', 'lb', 'rb', 'back', 'start', 'leftTrigger', 'rightTrigger']
/** The controls a widget kind can be given by hand. */
export const PAD_CHOICES: Readonly<Record<string, readonly PadSource[]>> = {
  joystick: ['leftStick', 'rightStick'],
  keyboard: ['dpad', 'leftStick', 'rightStick'],
  slider: ['leftTrigger', 'rightTrigger', 'lb', 'rb', 'leftStickX', 'leftStickY', 'rightStickX', 'rightStickY'],
  button: BUTTONS,
  switch: BUTTONS,
}

/** What a widget kind takes from the pad, in layout order. Kinds not listed are not driven by the pad. */
const ASSIGNMENTS: Readonly<Record<string, readonly PadSource[]>> = {
  joystick: ['leftStick', 'rightStick'],
  keyboard: ['dpad'],
  slider: ['rightTrigger', 'leftTrigger'],
  button: ['a', 'b', 'x', 'y'],
  switch: ['lb', 'rb', 'back', 'start'],
}

export const PAD_LABELS: Readonly<Record<PadSource, string>> = {
  leftStick: 'Left stick', rightStick: 'Right stick', leftStickX: 'Left stick X', leftStickY: 'Left stick Y', rightStickX: 'Right stick X', rightStickY: 'Right stick Y', dpad: 'D-pad', leftTrigger: 'LT', rightTrigger: 'RT',
  a: 'A', b: 'B', x: 'X', y: 'Y', lb: 'LB', rb: 'RB', back: 'Back', start: 'Start',
}

/**
 * Which pad control drives which widget (by widget ID). A control chosen by hand goes to that widget; the others of
 * a kind take the remaining controls of the kind's list in layout order. A widget set to `off` takes none.
 */
export const padSources = (widgets: readonly ControllerWidget[]): ReadonlyMap<string, PadSource> => {
  const result = new Map<string, PadSource>()
  const chosen = new Set<PadSource>()
  for (const widget of widgets) {
    if (isPadSource(widget.pad) && PAD_CHOICES[widget.kind]?.includes(widget.pad)) { result.set(widget.id, widget.pad); chosen.add(widget.pad) }
  }
  const next = new Map<string, number>()
  for (const widget of [...widgets].sort((a, b) => a.y - b.y || a.x - b.x)) {
    const list = ASSIGNMENTS[widget.kind]
    if (!list || widget.pad !== undefined) continue
    let at = next.get(widget.kind) ?? 0
    while (list[at] && chosen.has(list[at]!)) at++
    if (list[at]) result.set(widget.id, list[at]!)
    next.set(widget.kind, at + 1)
  }
  return result
}

/** The pad as the widgets see it: sticks −1..1 with up positive, triggers and buttons 0..1, the D-pad as −1 / 0 / 1. */
export interface PadState {
  readonly leftStick: { readonly x: number; readonly y: number }
  readonly rightStick: { readonly x: number; readonly y: number }
  readonly dpad: { readonly x: number; readonly y: number }
  readonly buttons: Readonly<Record<'leftTrigger' | 'rightTrigger' | 'a' | 'b' | 'x' | 'y' | 'lb' | 'rb' | 'back' | 'start', number>>
}

interface RawPad {
  readonly axes: readonly number[]
  readonly buttons: readonly { readonly value: number; readonly pressed?: boolean }[]
  /** The browser's name for the pad (it holds the vendor: `Vendor: 045e`) and its mapping (`standard`, or empty). */
  readonly id?: string
  readonly mapping?: string
}

/** Xbox pads that the browser does not map to the standard layout (an Xbox Wireless Controller over Bluetooth, say) report like this. */
const isXboxHid = (pad: RawPad): boolean => pad.mapping !== 'standard' && /vendor:\s*045e/i.test(pad.id ?? '') && pad.axes.length >= 6

/** The hat switch some pads use for the D-pad is one axis: eight positions from up, clockwise, in steps of 2/7 from −1; anything else is rest. */
const HAT: readonly (readonly [number, number])[] = [[0, 1], [1, 1], [1, 0], [1, -1], [0, -1], [-1, -1], [-1, 0], [-1, 1]]
const hatOf = (value: number | undefined): { x: number; y: number } => {
  if (value === undefined || value < -1.05 || value > 1.05) return { x: 0, y: 0 }
  const at = Math.round(((value + 1) * 7) / 2)
  const spot = HAT[at]
  return spot && Math.abs(-1 + (at * 2) / 7 - value) < 0.1 ? { x: spot[0], y: spot[1] } : { x: 0, y: 0 }
}

/**
 * Xbox pad in the raw layout: axes LX LY RX LT RT RY (triggers rest at −1, pulled +1) and the D-pad hat on axis 9;
 * buttons A B (2 unused) X Y (5 unused) LB RB (8, 9 unused) View Menu Xbox L3 R3.
 */
const xboxHidState = (pad: RawPad): PadState => {
  const button = (index: number): number => {
    const entry = pad.buttons[index]
    return entry ? Math.max(entry.value, entry.pressed ? 1 : 0) : 0
  }
  const axis = (index: number): number => pad.axes[index] ?? 0
  const trigger = (index: number): number => Math.min(1, Math.max(0, (axis(index) + 1) / 2))
  const hat = hatOf(pad.axes[9])
  return {
    leftStick: { x: axis(0), y: -axis(1) },
    rightStick: { x: axis(2), y: -axis(5) },
    dpad: { x: hat.x, y: hat.y },
    buttons: { a: button(0), b: button(1), x: button(3), y: button(4), lb: button(6), rb: button(7), leftTrigger: trigger(3), rightTrigger: trigger(4), back: button(10), start: button(11) },
  }
}

/** The standard mapping's indices: buttons A B X Y LB RB LT RT Back Start L3 R3 Up Down Left Right; axes LX LY RX RY. */
export const padStateOf = (pad: RawPad): PadState => {
  if (isXboxHid(pad)) return xboxHidState(pad)
  const button = (index: number): number => {
    const entry = pad.buttons[index]
    return entry ? Math.max(entry.value, entry.pressed ? 1 : 0) : 0
  }
  // A trigger is analog: the browser's `pressed` flag turns on at about 10 % pull, so using it would jump to 1.
  const analog = (index: number): number => pad.buttons[index]?.value ?? 0
  const axis = (index: number): number => pad.axes[index] ?? 0
  return {
    leftStick: { x: axis(0), y: -axis(1) },
    rightStick: { x: axis(2), y: -axis(3) },
    dpad: { x: button(15) > 0.5 ? 1 : button(14) > 0.5 ? -1 : 0, y: button(12) > 0.5 ? 1 : button(13) > 0.5 ? -1 : 0 },
    buttons: { a: button(0), b: button(1), x: button(2), y: button(3), lb: button(4), rb: button(5), leftTrigger: analog(6), rightTrigger: analog(7), back: button(8), start: button(9) },
  }
}

/** A stick position kept inside the unit circle (a square-gated stick reaches the corners). */
export const unitStick = (stick: { readonly x: number; readonly y: number }): { x: number; y: number } => {
  const length = Math.hypot(stick.x, stick.y)
  return length > 1 ? { x: stick.x / length, y: stick.y / length } : { x: stick.x, y: stick.y }
}
