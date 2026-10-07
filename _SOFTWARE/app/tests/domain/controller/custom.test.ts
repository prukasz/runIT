import { readFileSync } from 'node:fs'
import { afterEach, describe, expect, it } from 'vitest'
import { addCustomFile, addWidget, EMPTY_CONTROLLER, isMissingKind, parseController, removeCustomFile, setImportedWidgetTypes, widgetFromDefinition, widgetType } from '../../../src/domain/controller'
import { createProject, parseProject, serializeProject } from '../../../src/domain/project'
import { loadWidgetFile, loadWidgetFiles } from '../../../src/controller/customWidgets'

const example = readFileSync(new URL('../../../docs/controller/example.widget.js', import.meta.url), 'utf8')
const minimal = (extra = '') => `export default { kind: 'tiny', title: 'Tiny', mount() {}, ${extra} }`

afterEach(() => setImportedWidgetTypes(new Map()))

describe('widget definitions', () => {
  it('turns a description into a type with the defaults filled in', () => {
    const result = widgetFromDefinition({ kind: 'meter', title: 'Meter', mount: () => undefined, slots: [{ key: 'v', label: 'V' }], fields: [{ key: 'c', label: 'C', type: 'choice', choices: [['a', 'A'], ['b', 'B']], default: 'zzz' }] }, 'meter.js')
    expect('type' in result && result.type).toMatchObject({ kind: 'meter', file: 'meter.js', w: 3, h: 2, minW: 1, slots: [{ key: 'v', writes: false }], fields: [{ key: 'c', type: 'choice', default: 'a' }] })
  })

  it('keeps the keyboard flag: the palette marks such a widget as laptop only', () => {
    const type = (extra: object) => { const result = widgetFromDefinition({ kind: 'keys', title: 'Keys', mount() {}, ...extra }, 'keys.js'); return 'type' in result ? result.type : undefined }
    expect(type({ keyboard: true })?.keyboard).toBe(true)
    expect(type({})?.keyboard).toBeUndefined()
  })

  it('keeps the advanced list to settings the widget has', () => {
    const result = widgetFromDefinition({ kind: 'tiny', title: 'Tiny', mount() {}, fields: [{ key: 'a', label: 'A', type: 'bool' }, { key: 'b', label: 'B', type: 'bool' }], advanced: ['b', 'zzz', 7] }, 't.js')
    expect('type' in result && result.type.advanced).toEqual(['b'])
  })

  it('says what is wrong with a file, in the file author\'s terms', () => {
    const problem = (definition: unknown) => { const result = widgetFromDefinition(definition, 'x.js'); return 'error' in result ? result.error : 'ok' }
    expect(problem(undefined)).toMatch(/export default/)
    expect(problem({ kind: 'Bad Kind', title: 'T', mount() {} })).toMatch(/`kind`/)
    expect(problem({ kind: 'slider', title: 'T', mount() {} })).toMatch(/built-in/)
    expect(problem({ kind: 'tiny', mount() {} })).toMatch(/`title`/)
    expect(problem({ kind: 'tiny', title: 'T' })).toMatch(/mount/)
    expect(problem({ kind: 'tiny', title: 'T', mount() {}, slots: [{ key: 'a', label: 'A' }, { key: 'a', label: 'B' }] })).toMatch(/used twice/)
    expect(problem({ kind: 'tiny', title: 'T', mount() {}, fields: [{ key: 'n', label: 'N', type: 'number' }] })).toMatch(/numeric default/)
    expect(problem({ kind: 'tiny', title: 'T', mount() {}, fields: [{ key: 'n', label: 'N', type: 'colour' }] })).toMatch(/type must be/)
  })
})

describe('imported widget files', () => {
  it('loads the example file as a module', async () => {
    const result = await loadWidgetFile({ name: 'example.widget.js', source: example })
    expect('type' in result && result.type).toMatchObject({ kind: 'led-meter', title: 'LED meter', slots: [{ key: 'level', writes: true }] })
  })

  it('reports a file that does not run, and a kind two files claim', async () => {
    expect(await loadWidgetFile({ name: 'a.js', source: 'export default (' })).toHaveProperty('error')
    expect(await loadWidgetFile({ name: 'b.js', source: 'throw new Error("boom")' })).toEqual({ error: 'boom' })
    const both = await loadWidgetFiles([{ name: 'a.js', source: minimal() }, { name: 'b.js', source: minimal() }])
    expect([...both.types.keys()]).toEqual(['tiny'])
    expect(both.errors.get('b.js')).toMatch(/already provided by a\.js/)
  })

  it('runs the widget\'s mount and update against a plain element', async () => {
    const result = await loadWidgetFile({ name: 'example.widget.js', source: example })
    if (!('type' in result)) throw new Error(result.error)
    const written: [string, number][] = []
    const children: unknown[] = []
    const fake = () => ({ style: {}, addEventListener() {}, append(...nodes: unknown[]) { children.push(...nodes) }, replaceChildren() {}, set textContent(_: string) {} })
    ;(globalThis as { document?: unknown }).document = { createElement: fake }
    try {
      const instance = result.type.mount!(fake() as unknown as HTMLElement, { write: (slot, value) => written.push([slot, value]) })
      expect(instance && typeof instance === 'object' && typeof instance.update).toBe('function')
      expect(() => (instance as { update: (state: unknown) => void }).update({ values: { level: 40 }, options: { min: 0, max: 100, leds: 10, color: 'green' }, live: true, preview: false, width: 200, height: 80 })).not.toThrow()
    } finally {
      delete (globalThis as { document?: unknown }).document
    }
  })
})

describe('imported widgets in the controller', () => {
  it('keeps unknown kinds as they were, and names them while their file is missing', () => {
    const parsed = parseController({ widgets: [{ id: 'tiny1', kind: 'tiny', x: 1, y: 1, w: 3, h: 2, links: { v: 'obj-a' }, options: { n: 4, t: 'x', b: true, bad: { a: 1 } } }] })
    expect(parsed.widgets[0]).toMatchObject({ kind: 'tiny', links: { v: 'obj-a' }, options: { n: 4, t: 'x', b: true } })
    expect(parsed.widgets[0]!.options).not.toHaveProperty('bad')
    expect(isMissingKind('tiny')).toBe(true)
    expect(widgetType('tiny').title).toBe('tiny')
  })

  it('adds, replaces and removes files without touching the widgets, and saves them with the project', () => {
    const first = addCustomFile(EMPTY_CONTROLLER, { name: 'a.js', source: minimal() })
    const replaced = addCustomFile(first, { name: 'a.js', source: minimal('description: "new"') })
    expect(replaced.custom).toHaveLength(1)
    expect(replaced.custom![0]!.source).toContain('new')
    const withWidget = addWidget(replaced, 'slider').controller
    expect(withWidget.custom).toBe(replaced.custom)
    expect(removeCustomFile(withWidget, 'a.js').custom).toBeUndefined()
    expect(removeCustomFile(withWidget, 'other.js')).toBe(withWidget)
    const project = { ...createProject('t'), controller: { widgets: [], custom: replaced.custom } }
    expect(parseProject(serializeProject(project)).controller?.custom).toEqual(replaced.custom)
  })
})
