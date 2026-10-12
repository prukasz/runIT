import { isPadSource, PAD_CHOICES } from './gamepad'

/*
 * The controller (the Remote view): widgets on a coarse grid, each linked to
 * project variables. Pure data and edits; drawing and the live link live in
 * `controller/`. Widgets are described by their type (`WIDGET_TYPES`): the
 * variables they use (slots) and the settings the right bar offers (fields),
 * so the settings panel and the defaults need no per-widget code.
 */

/** Pixels per grid cell: the grid is rough on purpose, widgets are a few cells big. */
export const CELL = 48
export const COLS = 24
export const ROWS = 14

/** The built-in widgets; imported ones have kinds of their own (strings). */
export type WidgetKind = 'joystick' | 'slider' | 'switch' | 'button' | 'input' | 'keyboard' | 'display' | 'chart' | 'lamp' | 'bar' | 'gauge' | 'label'

export type WidgetOption = number | string | boolean

/** A variable a widget uses: `writes` = the widget sets it, otherwise it only shows it. */
export interface SlotSpec {
  readonly key: string
  readonly label: string
  readonly writes: boolean
  readonly hint?: string
}

interface FieldBase {
  readonly key: string
  readonly label: string
  readonly hint?: string
}

export type FieldSpec =
  | (FieldBase & { readonly type: 'number'; readonly default: number; readonly min?: number; readonly max?: number })
  | (FieldBase & { readonly type: 'choice'; readonly default: string; readonly choices: readonly (readonly [string, string])[] })
  | (FieldBase & { readonly type: 'bool'; readonly default: boolean })
  | (FieldBase & { readonly type: 'text'; readonly default: string })
  /** A keyboard key: the field listens for the next key press. */
  | (FieldBase & { readonly type: 'key'; readonly default: string })

/** What a custom widget's `mount` hands the board it draws on. */
export interface WidgetApi {
  /** Set the variable linked to a slot that `writes`. Throttled by the app; ignored for an unlinked slot. */
  readonly write: (slot: string, value: number) => void
}

/** What a custom widget is told, at mount and whenever something in it changes. */
export interface WidgetState {
  /** The live value of each slot's variable; undefined while the interface is not enabled or the slot is not linked. */
  readonly values: Readonly<Record<string, number | undefined>>
  /** The settings with their defaults filled in. */
  readonly options: Readonly<Record<string, WidgetOption>>
  /** The name the user gave this widget (empty when none): for labels a screen reader reads. */
  readonly name: string
  /** Whether each slot has a variable linked: `values` is undefined for an unlinked slot and while the interface is off, this tells which. */
  readonly linked: Readonly<Record<string, boolean>>
  /** The interface is enabled: the widget works. Otherwise it only shows itself (the layout is being edited). */
  readonly live: boolean
  readonly preview: boolean
  /** Size of the widget's area in pixels. */
  readonly width: number
  readonly height: number
}

export interface WidgetInstance {
  readonly update?: (state: WidgetState) => void
  readonly destroy?: () => void
}

export type WidgetMount = (element: HTMLElement, api: WidgetApi) => WidgetInstance | void

export interface WidgetType {
  /** A built-in kind, or the `kind` of an imported widget file. */
  readonly kind: string
  /** Set for an imported widget: its code, and the file it came from. */
  readonly mount?: WidgetMount
  readonly file?: string
  readonly title: string
  readonly description: string
  /** Size in cells when placed, and the least it may be resized to. */
  readonly w: number
  readonly h: number
  readonly minW: number
  readonly minH: number
  readonly slots: readonly SlotSpec[]
  readonly fields: readonly FieldSpec[]
  /** Keys of the slots and settings the panel keeps behind "Advanced". */
  readonly advanced?: readonly string[]
  /** Needs a physical keyboard: it does nothing on a phone. */
  readonly keyboard?: boolean
}

const range = (min: number, max: number): readonly FieldSpec[] => [
  { key: 'min', label: 'Lowest value', type: 'number', default: min },
  { key: 'max', label: 'Highest value', type: 'number', default: max },
]
const SNAP: readonly (readonly [string, string])[] = [['hold', 'Stay where left'], ['middle', 'Back to the middle'], ['min', 'Back to the lowest'], ['max', 'Back to the highest']]
const COLORS: readonly (readonly [string, string])[] = [['green', 'Green'], ['amber', 'Amber'], ['red', 'Red'], ['blue', 'Blue']]
/** Colours a widget may be drawn in: the theme's accent, or one of the status colours. `text` is the plain text colour. */
export const WIDGET_COLORS: readonly (readonly [string, string])[] = [['accent', 'Theme'], ['green', 'Green'], ['amber', 'Amber'], ['red', 'Red'], ['blue', 'Blue'], ['purple', 'Purple'], ['orange', 'Orange']]
const TEXT_COLORS: readonly (readonly [string, string])[] = [['text', 'Text'], ...WIDGET_COLORS]
const color = (fallback: string, label = 'Colour', key = 'color'): FieldSpec => ({ key, label, type: 'choice', default: fallback, choices: WIDGET_COLORS })

export const WIDGET_TYPES: Readonly<Record<WidgetKind, WidgetType>> = {
  joystick: {
    kind: 'joystick', advanced: ['deadzone', 'invertX', 'invertY', 'showValues'], title: 'Joystick', description: 'Two axes in one stick: drive, pan / tilt.', w: 5, h: 5, minW: 3, minH: 3,
    slots: [{ key: 'x', label: 'X axis', writes: true }, { key: 'y', label: 'Y axis', writes: true }],
    fields: [
      ...range(-100, 100),
      { key: 'snap', label: 'On release', type: 'choice', default: 'middle', choices: SNAP.slice(0, 2), hint: 'Back to the middle is what a drive stick wants; stay keeps the last position.' },
      { key: 'deadzone', label: 'Dead zone (%)', type: 'number', default: 8, min: 0, max: 90, hint: 'Near the middle the stick sends the middle value, so a drive does not creep.' },
      { key: 'invertX', label: 'Invert X', type: 'bool', default: false },
      { key: 'invertY', label: 'Invert Y', type: 'bool', default: false },
      color('accent', 'Stick colour'),
      { key: 'showValues', label: 'Show the X and Y values', type: 'bool', default: false },
    ],
  },
  slider: {
    kind: 'slider', advanced: ['step', 'snap', 'showValue', 'unit'], title: 'Slider', description: 'One value along a track: speed, brightness, angle.', w: 6, h: 2, minW: 2, minH: 2,
    slots: [{ key: 'value', label: 'Value', writes: true }],
    fields: [
      ...range(0, 100),
      { key: 'step', label: 'Step', type: 'number', default: 1, min: 0 },
      { key: 'orientation', label: 'Direction', type: 'choice', default: 'horizontal', choices: [['horizontal', 'Left to right'], ['vertical', 'Bottom to top']] },
      { key: 'snap', label: 'On release', type: 'choice', default: 'hold', choices: SNAP },
      { key: 'showValue', label: 'Show the value', type: 'bool', default: true },
      { key: 'unit', label: 'Unit', type: 'text', default: '' },
      color('accent'),
    ],
  },
  switch: {
    kind: 'switch', advanced: ['onValue', 'offValue', 'showState'], title: 'Switch', description: 'On / off that stays: a relay, a light.', w: 3, h: 2, minW: 2, minH: 2,
    slots: [{ key: 'value', label: 'Value', writes: true }],
    fields: [
      { key: 'onValue', label: 'Value when on', type: 'number', default: 1 },
      { key: 'offValue', label: 'Value when off', type: 'number', default: 0 },
      { key: 'label', label: 'Label', type: 'text', default: '' },
      color('green', 'Colour when on'),
      { key: 'showState', label: 'Show ON / OFF', type: 'bool', default: true },
    ],
  },
  button: {
    kind: 'button', advanced: ['pressValue', 'releaseValue'], title: 'Button', description: 'Sends a value while it is held: horn, fire, jog.', w: 3, h: 2, minW: 2, minH: 2,
    slots: [{ key: 'value', label: 'Value', writes: true }],
    fields: [
      { key: 'pressValue', label: 'Value while held', type: 'number', default: 1 },
      { key: 'releaseValue', label: 'Value when released', type: 'number', default: 0 },
      { key: 'label', label: 'Label', type: 'text', default: 'Hold' },
      color('accent'),
    ],
  },
  input: {
    kind: 'input', advanced: ['step', 'send'], title: 'Input field', description: 'Type a number and send it: a setpoint, a position.', w: 4, h: 2, minW: 3, minH: 2,
    slots: [{ key: 'value', label: 'Value', writes: true }],
    fields: [
      ...range(-1000000, 1000000),
      { key: 'step', label: 'Step of the arrows', type: 'number', default: 1, min: 0 },
      { key: 'unit', label: 'Unit', type: 'text', default: '' },
      { key: 'send', label: 'Send', type: 'choice', default: 'enter', choices: [['enter', 'On Enter / Send'], ['change', 'On every change']] },
    ],
  },
  keyboard: {
    kind: 'keyboard', advanced: ['amount', 'rest'], title: 'Keyboard', description: 'Keys drive two axes (laptop only): W A S D, arrows.', w: 5, h: 4, minW: 4, minH: 3, keyboard: true,
    slots: [{ key: 'x', label: 'Left / right', writes: true }, { key: 'y', label: 'Up / down', writes: true }],
    fields: [
      { key: 'left', label: 'Left key', type: 'key', default: 'a' },
      { key: 'right', label: 'Right key', type: 'key', default: 'd' },
      { key: 'up', label: 'Up key', type: 'key', default: 'w' },
      { key: 'down', label: 'Down key', type: 'key', default: 's' },
      { key: 'amount', label: 'Value while a key is held', type: 'number', default: 100 },
      { key: 'rest', label: 'Value with no key', type: 'number', default: 0 },
      color('accent', 'Colour of a pressed key'),
    ],
  },
  display: {
    kind: 'display', advanced: ['decimals', 'size'], title: 'Value', description: 'A number shown big: a sensor, a counter.', w: 4, h: 2, minW: 2, minH: 1,
    slots: [{ key: 'value', label: 'Value', writes: false }],
    fields: [
      { key: 'decimals', label: 'Decimals', type: 'number', default: 1, min: 0, max: 6 },
      { key: 'unit', label: 'Unit', type: 'text', default: '' },
      { key: 'label', label: 'Label', type: 'text', default: '' },
      { key: 'color', label: 'Colour', type: 'choice', default: 'text', choices: TEXT_COLORS },
      { key: 'size', label: 'Size', type: 'choice', default: 'normal', choices: [['small', 'Small'], ['normal', 'Normal'], ['large', 'Large']] },
    ],
  },
  chart: {
    kind: 'chart', advanced: ['value2', 'name2', 'color2', 'value3', 'name3', 'color3', 'line', 'fill', 'grid', 'points', 'stats', 'showLimit', 'limit', 'limitColor'], title: 'Chart', description: 'One to three values over the last seconds, with a grid, limit line and min / max / average.', w: 8, h: 4, minW: 4, minH: 3,
    slots: [
      { key: 'value', label: 'Series 1', writes: false },
      { key: 'value2', label: 'Series 2', writes: false, hint: 'Optional: a second line in the same chart.' },
      { key: 'value3', label: 'Series 3', writes: false, hint: 'Optional: a third line.' },
    ],
    fields: [
      { key: 'seconds', label: 'Window (seconds)', type: 'number', default: 20, min: 2, max: 600 },
      { key: 'auto', label: 'Fit the range to the data', type: 'bool', default: true },
      ...range(0, 100),
      { key: 'name', label: 'Name of series 1', type: 'text', default: '' },
      color('accent', 'Colour of series 1'),
      { key: 'name2', label: 'Name of series 2', type: 'text', default: '' },
      color('green', 'Colour of series 2', 'color2'),
      { key: 'name3', label: 'Name of series 3', type: 'text', default: '' },
      color('amber', 'Colour of series 3', 'color3'),
      { key: 'line', label: 'Line', type: 'choice', default: 'normal', choices: [['thin', 'Thin'], ['normal', 'Normal'], ['thick', 'Thick']] },
      { key: 'fill', label: 'Fill under the line', type: 'bool', default: true },
      { key: 'grid', label: 'Grid and time axis', type: 'bool', default: true },
      { key: 'points', label: 'Mark the newest point', type: 'bool', default: true },
      { key: 'stats', label: 'Show min / max / average', type: 'bool', default: true },
      { key: 'showLimit', label: 'Show a limit line', type: 'bool', default: false },
      { key: 'limit', label: 'Limit', type: 'number', default: 80 },
      color('red', 'Limit colour', 'limitColor'),
    ],
  },
  lamp: {
    kind: 'lamp', advanced: ['blink'], title: 'Lamp', description: 'A light for a state: running, fault, limit reached.', w: 2, h: 2, minW: 1, minH: 1,
    slots: [{ key: 'value', label: 'Value', writes: false }],
    fields: [
      { key: 'mode', label: 'Lit when the value is', type: 'choice', default: 'nonzero', choices: [['nonzero', 'Not zero (true)'], ['above', 'Above the limit'], ['below', 'Below the limit'], ['equals', 'Equal to the limit']] },
      { key: 'limit', label: 'Limit', type: 'number', default: 0 },
      { key: 'color', label: 'Colour when lit', type: 'choice', default: 'green', choices: [...COLORS, ['purple', 'Purple'], ['orange', 'Orange']] },
      { key: 'blink', label: 'Blink while lit', type: 'bool', default: false },
      { key: 'label', label: 'Label', type: 'text', default: '' },
    ],
  },
  bar: {
    kind: 'bar', advanced: ['warn', 'warnAt', 'warnColor'], title: 'Bar', description: 'A level between two values: battery, tank, load.', w: 6, h: 1, minW: 2, minH: 1,
    slots: [{ key: 'value', label: 'Value', writes: false }],
    fields: [
      ...range(0, 100),
      { key: 'orientation', label: 'Direction', type: 'choice', default: 'horizontal', choices: [['horizontal', 'Left to right'], ['vertical', 'Bottom to top']] },
      { key: 'unit', label: 'Unit', type: 'text', default: '' },
      color('accent'),
      { key: 'warn', label: 'Change colour above a level', type: 'bool', default: false },
      { key: 'warnAt', label: 'Level (% of the range)', type: 'number', default: 80, min: 0, max: 100 },
      color('red', 'Colour above the level', 'warnColor'),
    ],
  },
  gauge: {
    kind: 'gauge', advanced: ['warn', 'warnAt', 'warnColor'], title: 'Gauge', description: 'A dial between two values: speed, pressure, temperature.', w: 4, h: 3, minW: 2, minH: 2,
    slots: [{ key: 'value', label: 'Value', writes: false }],
    fields: [
      ...range(0, 100),
      { key: 'unit', label: 'Unit', type: 'text', default: '' },
      color('accent'),
      { key: 'warn', label: 'Change colour above a level', type: 'bool', default: false },
      { key: 'warnAt', label: 'Level (% of the range)', type: 'number', default: 80, min: 0, max: 100 },
      color('red', 'Colour above the level', 'warnColor'),
    ],
  },
  label: {
    kind: 'label', advanced: ['align'], title: 'Label', description: 'Text to name a group of widgets.', w: 4, h: 1, minW: 1, minH: 1,
    slots: [],
    fields: [
      { key: 'text', label: 'Text', type: 'text', default: 'Label' },
      { key: 'size', label: 'Size', type: 'choice', default: 'normal', choices: [['small', 'Small'], ['normal', 'Normal'], ['large', 'Large']] },
      { key: 'color', label: 'Colour', type: 'choice', default: 'text', choices: TEXT_COLORS },
      { key: 'align', label: 'Alignment', type: 'choice', default: 'left', choices: [['left', 'Left'], ['center', 'Centre']] },
    ],
  },
}

const MISSING_TYPE: WidgetType = { kind: '', title: '', description: 'Its widget file is not imported (any more): import it to use this widget again.', w: 2, h: 2, minW: 1, minH: 1, slots: [], fields: [] }
let imported: ReadonlyMap<string, WidgetType> = new Map()

/** The imported widget types, set by the app when it has loaded the files stored in the controller. */
export const setImportedWidgetTypes = (types: ReadonlyMap<string, WidgetType>): void => { imported = types }
export const importedWidgetTypes = (): readonly WidgetType[] => [...imported.values()]
export const isBuiltinKind = (kind: string): kind is WidgetKind => Object.hasOwn(WIDGET_TYPES, kind)
/** A widget's type; for a kind nothing provides (its file was removed) an empty one named after the kind. */
export const widgetType = (kind: string): WidgetType => (isBuiltinKind(kind) ? WIDGET_TYPES[kind] : imported.get(kind) ?? { ...MISSING_TYPE, kind, title: kind })
export const isMissingKind = (kind: string): boolean => !isBuiltinKind(kind) && !imported.has(kind)

/** The order the palette lists them in. */
export const WIDGET_KINDS: readonly WidgetKind[] = ['joystick', 'slider', 'switch', 'button', 'input', 'keyboard', 'display', 'chart', 'lamp', 'bar', 'gauge', 'label']

export interface ControllerWidget {
  readonly id: string
  readonly kind: string
  /** Position and size in cells. */
  readonly x: number
  readonly y: number
  readonly w: number
  readonly h: number
  readonly name?: string
  /** The gamepad control that drives it: absent = automatic (see gamepad.ts), `off` = none, else a `PadSource`. */
  readonly pad?: string
  /** Slot key → project variable ID. */
  readonly links: Readonly<Record<string, string>>
  /** Only what differs from the type's defaults. */
  readonly options: Readonly<Record<string, WidgetOption>>
}

/** An imported widget file, kept in the controller (so the project carries it and undo covers importing). */
export interface CustomWidgetFile {
  readonly name: string
  readonly source: string
}

export interface Controller {
  readonly widgets: readonly ControllerWidget[]
  readonly custom?: readonly CustomWidgetFile[]
}

export const MAX_WIDGET_SOURCE = 200_000

export const EMPTY_CONTROLLER: Controller = { widgets: [] }

const clamp = (value: number, low: number, high: number): number => Math.min(high, Math.max(low, value))

/** The value of a setting: the widget's own, else the type's default. */
export const optionOf = (widget: ControllerWidget, key: string): WidgetOption => {
  const own = widget.options[key]
  if (own !== undefined) return own
  const field = widgetType(widget.kind).fields.find((entry) => entry.key === key)
  return field ? field.default : ''
}
export const numberOption = (widget: ControllerWidget, key: string): number => {
  const value = optionOf(widget, key)
  return typeof value === 'number' && Number.isFinite(value) ? value : 0
}
export const textOption = (widget: ControllerWidget, key: string): string => String(optionOf(widget, key))
export const flagOption = (widget: ControllerWidget, key: string): boolean => optionOf(widget, key) === true

/** The widget's own name, else its type's. */
export const widgetTitle = (widget: ControllerWidget): string => widget.name || widgetType(widget.kind).title

/** Keep a widget inside the grid and no smaller than its type allows. */
export const fit = (widget: ControllerWidget): ControllerWidget => {
  const type = widgetType(widget.kind)
  const w = clamp(Math.round(widget.w), type.minW, COLS)
  const h = clamp(Math.round(widget.h), type.minH, ROWS)
  const x = clamp(Math.round(widget.x), 0, COLS - w)
  const y = clamp(Math.round(widget.y), 0, ROWS - h)
  return x === widget.x && y === widget.y && w === widget.w && h === widget.h ? widget : { ...widget, x, y, w, h }
}

const overlaps = (a: { x: number; y: number; w: number; h: number }, b: { x: number; y: number; w: number; h: number }): boolean =>
  a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h

/** The first free spot for a widget of this size, scanning rows then columns from `from`; the grid's corner when it is full. */
export const freeSpot = (controller: Controller, w: number, h: number, from: { x: number; y: number } = { x: 0, y: 0 }): { x: number; y: number } => {
  for (let y = from.y; y <= ROWS - h; y++) {
    for (let x = y === from.y ? from.x : 0; x <= COLS - w; x++) {
      if (!controller.widgets.some((widget) => overlaps(widget, { x, y, w, h }))) return { x, y }
    }
  }
  return { x: 0, y: 0 }
}

const newId = (controller: Controller, kind: string): string => {
  const taken = new Set(controller.widgets.map((widget) => widget.id))
  for (let n = 1; ; n++) if (!taken.has(`${kind}${n}`)) return `${kind}${n}`
}

/** A new widget of `kind`, at `at` (a drop) or the first free spot. */
export const addWidget = (controller: Controller, kind: string, at?: { x: number; y: number }): { controller: Controller; widget: ControllerWidget } => {
  const type = widgetType(kind)
  const spot = at ?? freeSpot(controller, type.w, type.h)
  const widget = fit({ id: newId(controller, kind), kind, x: spot.x, y: spot.y, w: type.w, h: type.h, links: {}, options: {} })
  return { controller: { ...controller, widgets: [...controller.widgets, widget] }, widget }
}

const change = (controller: Controller, id: string, edit: (widget: ControllerWidget) => ControllerWidget): Controller => {
  let touched = false
  const widgets = controller.widgets.map((widget) => {
    if (widget.id !== id) return widget
    const next = edit(widget)
    if (next !== widget) touched = true
    return next
  })
  return touched ? { ...controller, widgets } : controller
}

export const placeWidget = (controller: Controller, id: string, box: { x: number; y: number; w: number; h: number }): Controller =>
  change(controller, id, (widget) => fit({ ...widget, ...box }))

export const removeWidget = (controller: Controller, id: string): Controller =>
  controller.widgets.some((widget) => widget.id === id) ? { ...controller, widgets: controller.widgets.filter((widget) => widget.id !== id) } : controller

export const renameWidget = (controller: Controller, id: string, name: string): Controller =>
  change(controller, id, (widget) => {
    const next = name.trim()
    if ((widget.name ?? '') === next) return widget
    const { name: _, ...rest } = widget
    return next ? { ...rest, name: next } : rest
  })

/** Give a widget a gamepad control, `off` for none, or (undefined) the automatic one. */
export const setPad = (controller: Controller, id: string, pad: string | undefined): Controller =>
  change(controller, id, (widget) => {
    if (widget.pad === pad) return widget
    const { pad: _, ...rest } = widget
    return pad === undefined ? rest : { ...rest, pad }
  })

/** Link a slot to a variable, or (undefined) unlink it. */
export const linkWidget = (controller: Controller, id: string, slot: string, variableId: string | undefined): Controller =>
  change(controller, id, (widget) => {
    if (!widgetType(widget.kind).slots.some((entry) => entry.key === slot)) return widget
    if (widget.links[slot] === variableId) return widget
    const { [slot]: _, ...rest } = widget.links
    return { ...widget, links: variableId === undefined ? rest : { ...rest, [slot]: variableId } }
  })

/** Set one setting; a value equal to the default is dropped, so the widget stores only what the user changed. */
export const setOption = (controller: Controller, id: string, key: string, value: WidgetOption): Controller =>
  change(controller, id, (widget) => {
    const field = widgetType(widget.kind).fields.find((entry) => entry.key === key)
    if (!field || optionOf(widget, key) === value) return widget
    const { [key]: _, ...rest } = widget.options
    return { ...widget, options: value === field.default ? rest : { ...rest, [key]: value } }
  })

/** A copy beside the original (the first free spot at or after it), linked to the same variables. */
export const duplicateWidget = (controller: Controller, id: string): { controller: Controller; widget?: ControllerWidget } => {
  const source = controller.widgets.find((widget) => widget.id === id)
  if (!source) return { controller }
  const spot = freeSpot(controller, source.w, source.h, { x: Math.min(source.x + 1, COLS - source.w), y: source.y })
  const copy = fit({ ...source, id: newId(controller, source.kind), x: spot.x, y: spot.y })
  return { controller: { ...controller, widgets: [...controller.widgets, copy] }, widget: copy }
}

/** An imported file added, or replacing the one of the same name. */
export const addCustomFile = (controller: Controller, file: CustomWidgetFile): Controller => ({
  ...controller,
  custom: [...(controller.custom ?? []).filter((entry) => entry.name !== file.name), file],
})

export const removeCustomFile = (controller: Controller, name: string): Controller => {
  const rest = (controller.custom ?? []).filter((entry) => entry.name !== name)
  if (rest.length === (controller.custom ?? []).length) return controller
  const { custom: _, ...others } = controller
  return rest.length ? { ...others, custom: rest } : others
}

/** A variable was deleted: widgets stop pointing at it. */
export const unlinkVariable = (controller: Controller, variableId: string): Controller => {
  if (!controller.widgets.some((widget) => Object.values(widget.links).includes(variableId))) return controller
  return {
    ...controller,
    widgets: controller.widgets.map((widget) => {
      if (!Object.values(widget.links).includes(variableId)) return widget
      return { ...widget, links: Object.fromEntries(Object.entries(widget.links).filter(([, id]) => id !== variableId)) }
    }),
  }
}

/** Every variable the widgets use, once, and the ones they write. */
export const linkedVariables = (controller: Controller): { readonly all: readonly string[]; readonly written: readonly string[] } => {
  const all = new Set<string>()
  const written = new Set<string>()
  for (const widget of controller.widgets) {
    for (const slot of widgetType(widget.kind).slots) {
      const id = widget.links[slot.key]
      if (!id) continue
      all.add(id)
      if (slot.writes) written.add(id)
    }
  }
  return { all: [...all], written: [...written] }
}

/* ---- What a widget does with numbers ---- */

/** Where `fraction` (0..1) lies between `min` and `max`. */
export const lerp = (min: number, max: number, fraction: number): number => min + (max - min) * fraction

/** The 0..1 position of `value` between `min` and `max`, kept inside. */
export const fractionOf = (min: number, max: number, value: number): number => (max === min ? 0 : clamp((value - min) / (max - min), 0, 1))

/** A value rounded to `step` counted from `min`, kept inside the range. A zero step does not round. */
export const stepped = (value: number, min: number, max: number, step: number): number => {
  const low = Math.min(min, max)
  const high = Math.max(min, max)
  const rounded = step > 0 ? low + Math.round((value - low) / step) * step : value
  return clamp(Number(rounded.toFixed(6)), low, high)
}

/** What a stick or slider sends when released: its rest value, or undefined to stay where it is. */
export const restValue = (snap: string, min: number, max: number): number | undefined =>
  snap === 'middle' ? (min + max) / 2 : snap === 'min' ? min : snap === 'max' ? max : undefined

/** One stick axis, `position` in -1..1: nothing inside the dead zone (0..1 of the radius), the rest stretched over the range. */
export const axisValue = (position: number, min: number, max: number, deadzone: number, invert: boolean): number => {
  const signed = invert ? -position : position
  const magnitude = Math.abs(signed)
  const live = deadzone >= 1 ? 0 : Math.max(0, (magnitude - deadzone) / (1 - deadzone))
  return lerp(min, max, (Math.sign(signed) * live + 1) / 2)
}

/** Whether a lamp is lit for `value`: nothing known is not lit. */
export const lampLit = (value: number | undefined, mode: string, limit: number): boolean => {
  if (value === undefined || Number.isNaN(value)) return false
  if (mode === 'above') return value > limit
  if (mode === 'below') return value < limit
  if (mode === 'equals') return value === limit
  return value !== 0
}

/** A number as text with `decimals` places, trailing zeros kept so a changing value does not jitter. */
export const formatNumber = (value: number, decimals: number): string => (Number.isFinite(value) ? value.toFixed(clamp(Math.round(decimals), 0, 6)) : '–')

/** A key as the keyboard widget compares it: letters in lower case, the rest as `KeyboardEvent.key`. */
export const normalizeKey = (key: string): string => (key.length === 1 ? key.toLowerCase() : key)

/** What a key is called on screen. */
export const keyLabel = (key: string): string => (key === ' ' ? 'Space' : key.startsWith('Arrow') ? key.slice(5) : key.length === 1 ? key.toUpperCase() : key)

/* ---- Storage ---- */

/** What a widget kind may be called: a built-in, or the name an imported file gives its widget. */
export const KIND_PATTERN = /^[a-z][a-z0-9-]{1,31}$/

/** A controller from stored JSON; what does not make sense is dropped, so an old or edited file never breaks the view. */
export const parseController = (raw: unknown): Controller => {
  const record = typeof raw === 'object' && raw !== null ? raw as { widgets?: unknown; custom?: unknown } : {}
  const list = Array.isArray(record.widgets) ? record.widgets : []
  const seen = new Set<string>()
  const widgets: ControllerWidget[] = []
  const primitive = (value: unknown): value is WidgetOption => typeof value === 'number' ? Number.isFinite(value) : typeof value === 'string' || typeof value === 'boolean'
  for (const entry of list) {
    if (typeof entry !== 'object' || entry === null) continue
    const item = entry as Record<string, unknown>
    if (typeof item.id !== 'string' || seen.has(item.id) || typeof item.kind !== 'string' || !KIND_PATTERN.test(item.kind)) continue
    const kind = item.kind
    // An imported widget's slots and settings are known only once its file is loaded: keep what is plain, check it when it is used.
    const known = isBuiltinKind(kind) ? WIDGET_TYPES[kind] : undefined
    const type = widgetType(kind)
    const num = (value: unknown, fallback: number) => (typeof value === 'number' && Number.isFinite(value) ? value : fallback)
    const links: Record<string, string> = {}
    if (typeof item.links === 'object' && item.links !== null) {
      for (const [slot, id] of Object.entries(item.links as Record<string, unknown>)) {
        if (typeof id === 'string' && id && (!known || known.slots.some((entry) => entry.key === slot))) links[slot] = id
      }
    }
    const options: Record<string, WidgetOption> = {}
    if (typeof item.options === 'object' && item.options !== null) {
      for (const [key, value] of Object.entries(item.options as Record<string, unknown>)) {
        const field = known?.fields.find((entry) => entry.key === key)
        if (!known) { if (primitive(value)) options[key] = value; continue }
        if (!field) continue
        if (field.type === 'number' && typeof value === 'number' && Number.isFinite(value)) options[key] = value
        else if (field.type === 'bool' && typeof value === 'boolean') options[key] = value
        else if ((field.type === 'text' || field.type === 'key') && typeof value === 'string') options[key] = value
        else if (field.type === 'choice' && typeof value === 'string' && field.choices.some(([choice]) => choice === value)) options[key] = value
      }
    }
    seen.add(item.id)
    widgets.push(fit({
      id: item.id, kind, x: num(item.x, 0), y: num(item.y, 0), w: num(item.w, type.w), h: num(item.h, type.h),
      ...(typeof item.name === 'string' && item.name ? { name: item.name } : {}),
      ...(item.pad === 'off' || (isPadSource(item.pad) && PAD_CHOICES[kind]?.includes(item.pad)) ? { pad: item.pad as string } : {}), links, options,
    }))
  }
  const files = new Set<string>()
  const custom: CustomWidgetFile[] = []
  for (const entry of Array.isArray(record.custom) ? record.custom : []) {
    if (typeof entry !== 'object' || entry === null) continue
    const { name, source } = entry as Record<string, unknown>
    if (typeof name !== 'string' || !name || name.length > 120 || typeof source !== 'string' || !source || source.length > MAX_WIDGET_SOURCE || files.has(name)) continue
    files.add(name)
    custom.push({ name, source })
  }
  return custom.length ? { widgets, custom } : { widgets }
}
