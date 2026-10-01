import { DescriptorError } from './commandCatalog'
import type { GeneratedVmBlockFile, GeneratedVmBlockPin, GeneratedVmBlockStateField, GeneratedVmTemplatePart } from './generatedTypes'

/*
 * A VM block is two files in its firmware folder (components/VM/blocks/<name>/):
 *  - <name>.display.json: what the editor shows, written by hand (title, category, description, activation text, the `header`
 *    face line, `eno`, `always_detailed`, a title and description per pin by pin name);
 *  - <name>.content.json: what the block is, generated from its header (pins, shape, payload layout, enums, rules).
 * `composeVmBlockFile` joins them by pin and field name into the one object the palette is built from. It knows no block.
 */

export interface VmBlockDisplayFile {
  readonly title: string
  readonly category: string
  readonly description: string
  readonly activation?: string
  readonly header?: string
  readonly always_detailed?: boolean
  readonly eno?: { readonly title: string; readonly description?: string }
  readonly inputs?: Readonly<Record<string, { readonly title: string; readonly description?: string }>>
  readonly outputs?: Readonly<Record<string, { readonly title: string; readonly description?: string }>>
}

type ContentPin = Omit<GeneratedVmBlockPin, 'title' | 'description'>

export interface VmBlockContentFile extends Omit<GeneratedVmBlockFile, 'title' | 'category' | 'description' | 'eno' | 'always_detailed' | 'header' | 'inputs' | 'outputs' | 'activation'> {
  readonly activation: { readonly kind: string }
  readonly inputs: { readonly min: number; readonly max: number; readonly pins: readonly ContentPin[] }
  readonly outputs: { readonly min: number; readonly max: number; readonly pins: readonly ContentPin[] }
  readonly min_enables?: number
}

const REF = /\{(\w+)\}/g

/** A face line with `{name}` references as parts: text, the block title, an input or output pin, or a setting the user enters. */
const parseTemplate = (text: string, name: string, content: VmBlockContentFile): GeneratedVmTemplatePart[] => {
  const inputs = new Map(content.inputs.pins.map((pin) => [pin.name, pin]))
  const outputs = new Map(content.outputs.pins.map((pin) => [pin.name, pin]))
  const fields = new Map<string, GeneratedVmBlockStateField>((content.state?.fields ?? []).map((field) => [field.name, field]))
  const resolve = (ref: string): GeneratedVmTemplatePart => {
    if (ref === 'title') return { ref, kind: 'title' }
    const input = inputs.get(ref)
    if (input) {
      if ((input.index as unknown) === '*') throw new DescriptorError(`${name}: {${ref}} is a repeating pin and cannot be referenced in the header.`)
      return { ref, kind: 'pin', pin: input.index, ...(input.overrides ? { field: input.overrides } : {}) }
    }
    const output = outputs.get(ref)
    if (output) return { ref, kind: 'out', pin: output.index }
    const field = fields.get(ref)
    if (field && field.source === 'user' && !field.flexible) return { ref, kind: 'field', field: ref }
    throw new DescriptorError(`${name}: {${ref}} in the header names no pin or setting.`)
  }
  const parts: GeneratedVmTemplatePart[] = []
  let position = 0
  for (const match of text.matchAll(REF)) {
    if (match.index > position) parts.push({ text: text.slice(position, match.index) })
    parts.push(resolve(match[1]))
    position = match.index + match[0].length
  }
  if (position < text.length) parts.push({ text: text.slice(position) })
  const first = parts[0]
  if (first && 'text' in first) parts[0] = { text: first.text.trimStart() }
  const last = parts[parts.length - 1]
  if (last && 'text' in last) parts[parts.length - 1] = { text: last.text.trimEnd() }
  const kept = parts.filter((part) => !('text' in part) || part.text !== '')
  if (kept.some((part) => 'text' in part && /[{}]/.test(part.text))) throw new DescriptorError(`${name}: stray brace in header "${text}".`)
  return kept
}

const withTitles = (side: 'inputs' | 'outputs', content: VmBlockContentFile, display: VmBlockDisplayFile, name: string) => {
  const shown = display[side] ?? {}
  const pins = content[side].pins
  for (const key of Object.keys(shown)) if (!pins.some((pin) => pin.name === key)) throw new DescriptorError(`${name}.display.json lists ${side} "${key}", which the block has no pin for.`)
  return {
    min: content[side].min,
    max: content[side].max,
    pins: pins.map((pin) => {
      const entry = shown[pin.name]
      if (!entry?.title) throw new DescriptorError(`${name}.display.json needs a title for ${side} pin "${pin.name}".`)
      return { ...pin, title: entry.title, ...(entry.description ? { description: entry.description } : {}) }
    }),
  }
}

/** The block as the palette builder reads it: the content with the display's face and pin titles. */
export const composeVmBlockFile = (display: VmBlockDisplayFile, content: VmBlockContentFile): GeneratedVmBlockFile => {
  const name = content.name
  const bar = display.header?.indexOf('|') ?? -1
  const lead = display.header === undefined ? undefined : bar < 0 ? display.header : display.header.slice(0, bar)
  const value = display.header === undefined || bar < 0 ? undefined : display.header.slice(bar + 1)
  const header = lead === undefined ? undefined : {
    lead: parseTemplate(lead, name, content),
    ...(value !== undefined && value.trim() ? { value: parseTemplate(value, name, content) } : {}),
  }
  return {
    ...content,
    kind: 'vm-block',
    title: display.title,
    category: display.category,
    description: display.description,
    activation: { kind: content.activation.kind, ...(display.activation ? { description: display.activation } : {}) },
    inputs: withTitles('inputs', content, display, name),
    outputs: withTitles('outputs', content, display, name),
    ...(display.eno ? { eno: display.eno } : {}),
    ...(display.always_detailed ? { always_detailed: true } : {}),
    ...(header ? { header } : {}),
  }
}
