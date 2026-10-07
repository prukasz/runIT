import { isBuiltinKind, KIND_PATTERN } from './model'
import type { FieldSpec, SlotSpec, WidgetType } from './model'

/*
 * An imported widget is one .js file whose default export describes it (see
 * app/docs/controller-widgets.md): who it is, the variables it uses (slots),
 * the settings the right bar offers (fields), and `mount`, which draws it.
 * This checks that description and turns it into a `WidgetType`, so an
 * imported widget gets the same panel, linking, saving and undo as a built-in
 * one. Pure: loading the file is `controller/customWidgets.ts`.
 */

const KEY = /^[A-Za-z][A-Za-z0-9_]{0,31}$/
const text = (value: unknown, limit = 200): value is string => typeof value === 'string' && value.length > 0 && value.length <= limit
const whole = (value: unknown, min: number, max: number): value is number => typeof value === 'number' && Number.isInteger(value) && value >= min && value <= max

const slotFrom = (raw: unknown, at: string): SlotSpec | string => {
  if (typeof raw !== 'object' || raw === null) return `${at} is not an object`
  const slot = raw as Record<string, unknown>
  if (typeof slot.key !== 'string' || !KEY.test(slot.key)) return `${at}: key must be a short name (letters, digits, _)`
  if (!text(slot.label, 60)) return `${at}: label is missing`
  return { key: slot.key, label: slot.label, writes: slot.writes === true, ...(typeof slot.hint === 'string' ? { hint: slot.hint } : {}) }
}

const fieldFrom = (raw: unknown, at: string): FieldSpec | string => {
  if (typeof raw !== 'object' || raw === null) return `${at} is not an object`
  const field = raw as Record<string, unknown>
  if (typeof field.key !== 'string' || !KEY.test(field.key)) return `${at}: key must be a short name (letters, digits, _)`
  if (!text(field.label, 60)) return `${at}: label is missing`
  const base = { key: field.key, label: field.label, ...(typeof field.hint === 'string' ? { hint: field.hint } : {}) }
  switch (field.type) {
    case 'number':
      if (typeof field.default !== 'number' || !Number.isFinite(field.default)) return `${at}: a number field needs a numeric default`
      return { ...base, type: 'number', default: field.default, ...(typeof field.min === 'number' ? { min: field.min } : {}), ...(typeof field.max === 'number' ? { max: field.max } : {}) }
    case 'bool':
      return { ...base, type: 'bool', default: field.default === true }
    case 'text':
      return { ...base, type: 'text', default: typeof field.default === 'string' ? field.default : '' }
    case 'key':
      return { ...base, type: 'key', default: typeof field.default === 'string' ? field.default : '' }
    case 'choice': {
      const choices = Array.isArray(field.choices) ? field.choices : []
      const pairs = choices.map((choice): readonly [string, string] | undefined => (Array.isArray(choice) && typeof choice[0] === 'string' && typeof choice[1] === 'string' ? [choice[0], choice[1]] : undefined))
      if (pairs.length < 2 || pairs.some((pair) => !pair)) return `${at}: a choice field needs choices, a list of [value, label] pairs`
      const list = pairs as readonly (readonly [string, string])[]
      const fallback = typeof field.default === 'string' && list.some(([value]) => value === field.default) ? field.default : list[0]![0]
      return { ...base, type: 'choice', default: fallback, choices: list }
    }
    default:
      return `${at}: type must be number, bool, text, key or choice`
  }
}

/** The widget type a file describes, or what is wrong with it. `file` is the name it was imported under. */
export const widgetFromDefinition = (definition: unknown, file: string): { readonly type: WidgetType } | { readonly error: string } => {
  if (typeof definition !== 'object' || definition === null) return { error: 'the file must `export default` an object describing the widget' }
  const def = definition as Record<string, unknown>
  if (typeof def.kind !== 'string' || !KIND_PATTERN.test(def.kind)) return { error: '`kind` must be 2 to 32 characters: a lower-case letter, then letters, digits or -' }
  if (isBuiltinKind(def.kind)) return { error: `\`kind\` "${def.kind}" is a built-in widget: pick another name` }
  if (!text(def.title, 40)) return { error: '`title` is missing' }
  if (typeof def.mount !== 'function') return { error: '`mount(element, api)` is missing: it draws the widget' }
  const size = typeof def.size === 'object' && def.size !== null ? def.size as Record<string, unknown> : {}
  const minW = whole(size.minW, 1, 24) ? size.minW : 1
  const minH = whole(size.minH, 1, 14) ? size.minH : 1
  const w = whole(size.w, minW, 24) ? size.w : Math.max(minW, 3)
  const h = whole(size.h, minH, 14) ? size.h : Math.max(minH, 2)
  const slots: SlotSpec[] = []
  for (const [index, raw] of (Array.isArray(def.slots) ? def.slots : []).entries()) {
    const slot = slotFrom(raw, `slots[${index}]`)
    if (typeof slot === 'string') return { error: slot }
    if (slots.some((entry) => entry.key === slot.key)) return { error: `slots[${index}]: "${slot.key}" is used twice` }
    slots.push(slot)
  }
  const fields: FieldSpec[] = []
  for (const [index, raw] of (Array.isArray(def.fields) ? def.fields : []).entries()) {
    const field = fieldFrom(raw, `fields[${index}]`)
    if (typeof field === 'string') return { error: field }
    if (fields.some((entry) => entry.key === field.key)) return { error: `fields[${index}]: "${field.key}" is used twice` }
    fields.push(field)
  }
  const advanced = (Array.isArray(def.advanced) ? def.advanced : []).filter((key): key is string => typeof key === 'string' && (fields.some((field) => field.key === key) || slots.some((slot) => slot.key === key)))
  return {
    type: {
      kind: def.kind, title: def.title, description: typeof def.description === 'string' ? def.description : 'An imported widget.', w, h, minW, minH, slots, fields,
      mount: def.mount as WidgetType['mount'], file,
      ...(def.keyboard === true ? { keyboard: true } : {}),
      ...(advanced.length ? { advanced } : {}),
    },
  }
}
