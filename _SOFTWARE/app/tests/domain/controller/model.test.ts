import { describe, expect, it } from 'vitest'
import { createProject, parseProject, serializeProject } from '../../../src/domain/project'
import { addWidget, axisValue, COLS, duplicateWidget, EMPTY_CONTROLLER, fit, formatNumber, fractionOf, freeSpot, lampLit, linkedVariables, linkWidget, numberOption, parseController, placeWidget, removeWidget, restValue, setOption, stepped, unlinkVariable, WIDGET_KINDS, WIDGET_TYPES } from '../../../src/domain/controller'

describe('controller model', () => {
  it('describes every kind, with defaults for every field and a slot only where it reads or writes', () => {
    for (const kind of WIDGET_KINDS) {
      const type = WIDGET_TYPES[kind]
      expect(type.kind).toBe(kind)
      expect(type.w).toBeGreaterThanOrEqual(type.minW)
      expect(type.h).toBeGreaterThanOrEqual(type.minH)
      expect(new Set(type.fields.map((field) => field.key)).size).toBe(type.fields.length)
    }
    expect(WIDGET_TYPES.joystick.slots.map((slot) => slot.key)).toEqual(['x', 'y'])
    // The chart opens with one series; the second and third (variable, name, colour) are behind Advanced.
    expect(WIDGET_TYPES.chart.advanced).toEqual(expect.arrayContaining(['value2', 'name2', 'color2', 'value3', 'name3', 'color3']))
    expect(WIDGET_TYPES.chart.advanced).not.toEqual(expect.arrayContaining(['value']))
    expect(WIDGET_TYPES.chart.advanced).not.toContain('color')
    for (const type of Object.values(WIDGET_TYPES)) for (const key of type.advanced ?? []) expect([...type.slots, ...type.fields].some((entry) => entry.key === key)).toBe(true)
    expect(WIDGET_TYPES.display.slots.every((slot) => !slot.writes)).toBe(true)
  })

  it('places new widgets on free spots and numbers their ids', () => {
    const one = addWidget(EMPTY_CONTROLLER, 'joystick')
    const two = addWidget(one.controller, 'joystick')
    expect([one.widget.id, two.widget.id]).toEqual(['joystick1', 'joystick2'])
    expect(two.widget.x).toBeGreaterThanOrEqual(one.widget.x + one.widget.w)
    expect(freeSpot(EMPTY_CONTROLLER, 4, 2)).toEqual({ x: 0, y: 0 })
  })

  it('keeps a widget inside the grid and above its least size', () => {
    const { widget } = addWidget(EMPTY_CONTROLLER, 'slider')
    expect(fit({ ...widget, x: 99, y: -4, w: 0, h: 99 })).toMatchObject({ x: COLS - 2, y: 0, w: 2 })
    const moved = placeWidget({ widgets: [widget] }, widget.id, { x: 3, y: 2, w: 1, h: 1 })
    expect(moved.widgets[0]).toMatchObject({ x: 3, y: 2, w: WIDGET_TYPES.slider.minW, h: WIDGET_TYPES.slider.minH })
  })

  it('stores only the settings that differ from the default', () => {
    const { controller, widget } = addWidget(EMPTY_CONTROLLER, 'slider')
    const changed = setOption(controller, widget.id, 'max', 255)
    expect(changed.widgets[0]!.options).toEqual({ max: 255 })
    expect(numberOption(changed.widgets[0]!, 'max')).toBe(255)
    expect(numberOption(changed.widgets[0]!, 'min')).toBe(0)
    expect(setOption(changed, widget.id, 'max', 100).widgets[0]!.options).toEqual({})
    expect(setOption(changed, widget.id, 'nothing', 1)).toBe(changed)
  })

  it('links slots to variables, copies with the links, and forgets a deleted variable', () => {
    const { controller, widget } = addWidget(EMPTY_CONTROLLER, 'joystick')
    const linked = linkWidget(linkWidget(controller, widget.id, 'x', 'obj-a'), widget.id, 'y', 'obj-b')
    expect(linkWidget(linked, widget.id, 'z', 'obj-c')).toBe(linked)
    expect(linkedVariables(linked)).toEqual({ all: ['obj-a', 'obj-b'], written: ['obj-a', 'obj-b'] })
    const copy = duplicateWidget(linked, widget.id)
    expect(copy.widget).toMatchObject({ id: 'joystick2', links: { x: 'obj-a', y: 'obj-b' } })
    expect(unlinkVariable(copy.controller, 'obj-a').widgets.every((entry) => entry.links.x === undefined && entry.links.y === 'obj-b')).toBe(true)
    expect(removeWidget(copy.controller, 'joystick1').widgets.map((entry) => entry.id)).toEqual(['joystick2'])
  })

  it('maps a stick position to the range with a dead zone, and rests where asked', () => {
    expect(axisValue(0, -100, 100, 0.1, false)).toBe(0)
    expect(axisValue(0.05, -100, 100, 0.1, false)).toBe(0)
    expect(axisValue(1, -100, 100, 0.1, false)).toBe(100)
    expect(axisValue(-1, -100, 100, 0.1, true)).toBe(100)
    expect(axisValue(0.55, 0, 100, 0.1, false)).toBeCloseTo(50 + 50 * 0.5, 5)
    expect(restValue('middle', -100, 100)).toBe(0)
    expect(restValue('min', 10, 20)).toBe(10)
    expect(restValue('hold', 10, 20)).toBeUndefined()
  })

  it('rounds to a step inside the range and reads fractions', () => {
    expect(stepped(7.4, 0, 100, 5)).toBe(5)
    expect(stepped(130, 0, 100, 5)).toBe(100)
    expect(stepped(3.14159, 0, 10, 0)).toBeCloseTo(3.14159, 5)
    expect(fractionOf(0, 200, 50)).toBe(0.25)
    expect(fractionOf(5, 5, 9)).toBe(0)
  })

  it('lights a lamp by its rule, and shows nothing for no value', () => {
    expect(lampLit(2, 'nonzero', 0)).toBe(true)
    expect(lampLit(0, 'nonzero', 0)).toBe(false)
    expect(lampLit(5, 'above', 4)).toBe(true)
    expect(lampLit(5, 'below', 4)).toBe(false)
    expect(lampLit(4, 'equals', 4)).toBe(true)
    expect(lampLit(undefined, 'nonzero', 0)).toBe(false)
    expect(formatNumber(3.14159, 2)).toBe('3.14')
    expect(formatNumber(Number.NaN, 2)).toBe('–')
  })

  it('reads stored JSON leniently: bad entries are dropped, settings are checked against the type', () => {
    const parsed = parseController({ widgets: [
      { id: 'slider1', kind: 'slider', x: 1, y: 1, w: 6, h: 2, links: { value: 'obj-a', other: 'obj-z' }, options: { max: 9, orientation: 'sideways', showValue: false, bogus: 1 } },
      { id: 'slider1', kind: 'slider' },
      { id: 'x', kind: 'Not A Kind!' },
      'nonsense',
    ] })
    expect(parsed.widgets).toHaveLength(1)
    expect(parsed.widgets[0]).toMatchObject({ links: { value: 'obj-a' }, options: { max: 9, showValue: false } })
    expect(parseController(undefined)).toEqual(EMPTY_CONTROLLER)
  })

  it('is saved with the project and comes back, and a project without widgets stores nothing', () => {
    const { controller, widget } = addWidget(EMPTY_CONTROLLER, 'slider')
    const project = { ...createProject('t'), controller: linkWidget(setOption(controller, widget.id, 'max', 255), widget.id, 'value', 'obj-a') }
    const back = parseProject(serializeProject(project))
    expect(back.controller).toEqual(project.controller)
    expect(serializeProject(createProject('t'))).not.toContain('controller')
    expect(serializeProject({ ...createProject('t'), controller: EMPTY_CONTROLLER })).not.toContain('controller')
  })
})
