import { readdirSync, readFileSync } from 'node:fs'
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { WIDGET_KINDS, WIDGET_TYPES } from '../../src/domain/controller'
import type { WidgetInstance, WidgetState, WidgetType } from '../../src/domain/controller'
import { loadWidgetFile } from '../../src/controller/customWidgets'

/*
 * app/docs/controller/widgets/: every built-in widget as an importable file. Each must load like any imported file,
 * describe the same slots, settings and size as the built-in one, and do the same with the same input. There is no DOM
 * in the test run, so a small stand-in records what the files build and lets the tests fire the events they listen to.
 */

const folder = new URL('../../docs/controller/widgets/', import.meta.url)
const source = (kind: string): string => readFileSync(new URL(`${kind}.widget.js`, folder), 'utf8')

interface Node {
  style: Record<string, string>
  children: Node[]
  textContent: string
  disabled: boolean
  value: string
  title: string
  handlers: Record<string, ((event: Record<string, unknown>) => void)[]>
  attrs: Record<string, string>
  append: (...nodes: Node[]) => void
  replaceChildren: (...nodes: Node[]) => void
  setAttribute: (name: string, value: string) => void
  addEventListener: (type: string, handler: (event: Record<string, unknown>) => void) => void
  setPointerCapture: () => void
  getBoundingClientRect: () => { left: number; top: number; width: number; height: number }
  fire: (type: string, event?: Record<string, unknown>) => void
}

const node = (): Node => {
  const self: Node = {
    style: {}, children: [], textContent: '', disabled: false, value: '', title: '', handlers: {}, attrs: {},
    append: (...nodes) => { self.children.push(...nodes) },
    replaceChildren: (...nodes) => { self.children = nodes },
    setAttribute: (name, value) => { self.attrs[name] = value },
    addEventListener: (type, handler) => { (self.handlers[type] ??= []).push(handler) },
    setPointerCapture: () => undefined,
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 100, height: 100 }),
    fire: (type, event = {}) => { for (const handler of self.handlers[type] ?? []) handler({ preventDefault: () => undefined, pointerId: 1, ...event }) },
  }
  return self
}

const windowHandlers: Record<string, ((event: Record<string, unknown>) => void)[]> = {}
beforeEach(() => {
  for (const key of Object.keys(windowHandlers)) delete windowHandlers[key]
  vi.stubGlobal('document', { createElement: node, createElementNS: node })
  vi.stubGlobal('HTMLElement', class {})
  vi.stubGlobal('window', {
    addEventListener: (type: string, handler: (event: Record<string, unknown>) => void) => { (windowHandlers[type] ??= []).push(handler) },
    removeEventListener: (type: string, handler: (event: Record<string, unknown>) => void) => { windowHandlers[type] = (windowHandlers[type] ?? []).filter((entry) => entry !== handler) },
  })
})
afterEach(() => { vi.unstubAllGlobals(); vi.useRealTimers() })

const load = async (kind: string): Promise<WidgetType> => {
  const result = await loadWidgetFile({ name: `${kind}.widget.js`, source: source(kind) })
  if (!('type' in result)) throw new Error(result.error)
  return result.type
}

const defaults = (type: WidgetType): Record<string, number | string | boolean> => Object.fromEntries(type.fields.map((field) => [field.key, field.default]))
const stateOf = (type: WidgetType, over: Partial<WidgetState> & { options?: Record<string, number | string | boolean> } = {}): WidgetState => ({
  values: {}, name: '', linked: Object.fromEntries(type.slots.map((slot) => [slot.key, true])), live: true, preview: false, width: 200, height: 100, ...over, options: { ...defaults(type), ...over.options },
})
const mount = async (kind: string, over: Parameters<typeof stateOf>[1] = {}) => {
  const type = await load(kind)
  const writes: [string, number][] = []
  const element = node()
  const instance = type.mount!(element as unknown as HTMLElement, { write: (slot, value) => { writes.push([slot, value]) } }) as WidgetInstance
  instance.update?.(stateOf(type, over))
  return { type, element, instance, writes, update: (next: Parameters<typeof stateOf>[1]) => instance.update?.(stateOf(type, next)) }
}

describe('exported widget files', () => {
  it('has one file for every built-in widget', () => {
    expect(readdirSync(folder).filter((name) => name.endsWith('.widget.js')).map((name) => name.replace('.widget.js', '')).sort()).toEqual([...WIDGET_KINDS].sort())
  })

  it.each(WIDGET_KINDS)('%s loads, and describes what the built-in widget does', async (kind) => {
    const type = await load(kind)
    const builtin = WIDGET_TYPES[kind]
    expect(type.kind).toBe(`${kind}-js`)
    expect(type.slots).toEqual(builtin.slots)
    expect(type.fields).toEqual(builtin.fields)
    expect(type.keyboard).toBe(builtin.keyboard)
    expect(type.advanced).toEqual(builtin.advanced)
    expect([type.w, type.h, type.minW, type.minH]).toEqual([builtin.w, builtin.h, builtin.minW, builtin.minH])
  })
})

describe('exported widgets behave like the built-in ones', () => {
  it('joystick: full right sends the top of X and the middle of Y, letting go returns both to the middle', async () => {
    const { element, writes } = await mount('joystick')
    const pad = element.children[0]!
    pad.fire('pointerdown', { clientX: 100, clientY: 50 })
    expect(writes.slice(-2)).toEqual([['x', 100], ['y', 0]])
    pad.fire('pointerup')
    expect(writes.slice(-2)).toEqual([['x', 0], ['y', 0]])
  })

  it('joystick: the layout editor (preview) does nothing', async () => {
    const { element, writes } = await mount('joystick', { preview: true })
    element.children[0]!.fire('pointerdown', { clientX: 100, clientY: 50 })
    expect(writes).toEqual([])
  })

  it('slider: a touch on the track writes the value under the finger, rounded to the step', async () => {
    const { element, writes } = await mount('slider')
    const track = element.children[0]!
    track.fire('pointerdown', { clientX: 55.4, clientY: 0 })
    expect(writes).toEqual([['value', 55]])
    track.fire('pointermove', { clientX: 100, clientY: 0 })
    track.fire('pointerup')
    expect(writes).toEqual([['value', 55], ['value', 100]])
  })

  it('slider: the layout editor does nothing, and shows the slider part way', async () => {
    const { element, writes } = await mount('slider', { preview: true })
    const track = element.children[0]!
    track.fire('pointerdown', { clientX: 10, clientY: 0 })
    expect(writes).toEqual([])
    expect(track.attrs['aria-valuenow']).toBe('40')
  })

  it('slider: arrow keys step the value', async () => {
    const { element, writes } = await mount('slider', { options: { step: 5 }, values: { value: 20 } })
    element.children[0]!.fire('keydown', { key: 'ArrowRight' })
    expect(writes).toEqual([['value', 25]]) // one step up from the board's 20
  })

  it('slider and input: label themselves with the name the user gave the widget', async () => {
    for (const [kind, holder] of [['slider', (n: Node) => n.children[0]!], ['input', (n: Node) => n.children[0]!.children[0]!]] as const) {
      const { element, update } = await mount(kind, { name: 'Speed' })
      expect(holder(element).attrs['aria-label']).toBe('Speed')
      update({ name: '' })
      expect(holder(element).attrs['aria-label']).not.toBe('Speed')
    }
  })

  it('slider: follows the board while it is not held', async () => {
    const { element, update } = await mount('slider')
    update({ values: { value: 30 } })
    expect(element.children[0]!.attrs['aria-valuenow']).toBe('30')
  })

  it('switch: a click sends the on value, the next one the off value', async () => {
    const { element, writes, update } = await mount('switch', { options: { onValue: 5, offValue: 2 } })
    const button = element.children[0]!
    button.fire('click')
    button.fire('click')
    expect(writes).toEqual([['value', 5], ['value', 2]])
    update({ options: { onValue: 5, offValue: 2 }, values: { value: 5 } })
    expect(button.attrs['aria-checked']).toBe('true')
  })

  it('button: sends the press value while held and the release value on letting go', async () => {
    const { element, writes } = await mount('button', { options: { pressValue: 7, releaseValue: 1 } })
    const button = element.children[0]!
    button.fire('pointerdown')
    button.fire('pointerup')
    button.fire('pointerup')
    expect(writes).toEqual([['value', 7], ['value', 1]])
  })

  it('button: a held button lets go when the widget goes away', async () => {
    const { element, writes, instance } = await mount('button')
    element.children[0]!.fire('pointerdown')
    instance.destroy?.()
    expect(writes).toEqual([['value', 1], ['value', 0]])
  })

  it('input: sends on submit when the number is in range, not before', async () => {
    const { element, writes } = await mount('input', { options: { min: 0, max: 100 } })
    const form = element.children[0]!
    const field = form.children[0]!
    field.value = '42'
    field.fire('input')
    expect(writes).toEqual([])
    form.fire('submit')
    expect(writes).toEqual([['value', 42]])
    field.value = '500'
    field.fire('input')
    form.fire('submit')
    expect(writes).toEqual([['value', 42]])
  })

  it('keyboard: a key drives its axis while held, and only while the interface works', async () => {
    const { writes, update } = await mount('keyboard')
    const press = (type: string, key: string) => { for (const handler of windowHandlers[type] ?? []) handler({ key, target: {}, ctrlKey: false, metaKey: false, altKey: false, preventDefault: () => undefined }) }
    press('keydown', 'D')
    expect(writes.slice(-2)).toEqual([['x', 100], ['y', 0]])
    press('keyup', 'D')
    expect(writes.slice(-2)).toEqual([['x', 0], ['y', 0]])
    const count = writes.length
    update({ live: false })
    press('keydown', 'w')
    expect(writes.length).toBe(count)
  })

  it('keyboard: removes its window listeners when it goes away', async () => {
    const { instance } = await mount('keyboard')
    instance.destroy?.()
    expect(Object.values(windowHandlers).flat()).toEqual([])
  })

  it('display: shows the value with its decimals and unit, a dash while unknown', async () => {
    const { element, update } = await mount('display', { options: { decimals: 2, unit: 'V' }, values: { value: 12.345 } })
    const number = element.children[1]!
    expect(number.children[0]!.textContent).toBe('12.35')
    expect(number.children[1]!.textContent).toBe('V')
    update({ values: {} })
    expect(number.children[0]!.textContent).toBe('–')
  })

  it('lamp: lit by the same rule as the built-in lamp', async () => {
    const { element, update } = await mount('lamp', { options: { mode: 'above', limit: 5, color: 'red' }, values: { value: 6 } })
    const bulb = element.children[0]!
    expect(bulb.style.boxShadow).toContain('--color-red')
    update({ options: { mode: 'above', limit: 5, color: 'red' }, values: { value: 5 } })
    expect(bulb.style.boxShadow).toBe('none')
  })

  it('bar: fills in proportion to the value', async () => {
    const { element } = await mount('bar', { options: { min: 0, max: 200 }, values: { value: 50 } })
    const fill = element.children[0]!.children[0]!
    expect(fill.style.width).toBe('25%')
  })

  it('label: shows its text', async () => {
    const { element } = await mount('label', { options: { text: 'Drive', size: 'large' } })
    expect(element.textContent).toBe('Drive')
    expect(element.style.fontSize).toBe('24px')
  })

  it('chart: keeps a sample every 200 ms while live, shows it in the legend, and stops its clock when it goes away', async () => {
    vi.useFakeTimers()
    const { element, instance } = await mount('chart', { values: { value: 40 }, options: { name: 'Speed' } })
    const key = () => element.children[0]!.children[0]!
    expect(key().children[2]!.textContent).toBe('–')
    vi.advanceTimersByTime(450)
    expect(key().children[1]!.textContent).toBe('Speed')
    expect(key().children[2]!.textContent).toBe('40.0') // one value: the fitted range is a single unit wide
    instance.destroy?.()
    expect(vi.getTimerCount()).toBe(0)
  })

  it('chart: draws a preview wave while the layout is edited, and a line per linked series', async () => {
    const { element, update } = await mount('chart', { preview: true, live: false, linked: { value: true, value2: false, value3: false } })
    const svg = () => element.children[1]!.children[1]!.children[0]!
    const lines = () => svg().children.flatMap((node) => node.children).filter((node) => node.attrs.points !== undefined && node.style.fill === 'none')
    expect(lines()).toHaveLength(1)
    update({ preview: true, live: false, linked: { value: true, value2: true, value3: true } })
    expect(lines()).toHaveLength(3)
  })

  it('chart: a limit line is drawn only when asked for', async () => {
    const { element, update } = await mount('chart', { preview: true, live: false })
    const dashed = () => element.children[1]!.children[1]!.children[0]!.children.filter((node) => node.attrs['stroke-dasharray'] !== undefined)
    expect(dashed()).toHaveLength(0)
    update({ preview: true, live: false, options: { showLimit: true, limit: 50, limitColor: 'amber' } })
    expect(dashed()).toHaveLength(1)
    expect(dashed()[0]!.style.stroke).toContain('--color-amber')
  })

  it('colour: the slider fill, the switch track and the bar fill take the colour the user chose', async () => {
    const slider = await mount('slider', { options: { color: 'red' } })
    expect(slider.element.children[0]!.children[0]!.style.background).toBe('var(--color-red)')
    const toggle = await mount('switch', { preview: true, options: { color: 'purple' } })
    expect(toggle.element.children[0]!.children[0]!.style.background).toBe('var(--color-purple)') // the layout editor shows it on
    const bar = await mount('bar', { options: { color: 'blue', warn: true, warnAt: 50, warnColor: 'orange', min: 0, max: 100 }, values: { value: 20 } })
    const fill = bar.element.children[0]!.children[0]!
    expect(fill.style.background).toBe('var(--color-blue)')
    bar.update({ options: { color: 'blue', warn: true, warnAt: 50, warnColor: 'orange', min: 0, max: 100 }, values: { value: 70 } })
    expect(fill.style.background).toBe('var(--color-orange)')
  })
})
