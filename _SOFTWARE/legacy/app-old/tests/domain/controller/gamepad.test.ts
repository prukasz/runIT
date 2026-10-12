import { describe, expect, it } from 'vitest'
import { padSources, padStateOf, parseController, setPad, unitStick } from '../../../src/domain/controller'
import type { ControllerWidget } from '../../../src/domain/controller'

const widget = (id: string, kind: string, x: number, y: number): ControllerWidget => ({ id, kind, x, y, w: 2, h: 2, links: {}, options: {} })
const pressed = (indices: number[], axes: number[] = [0, 0, 0, 0]) => ({ axes, buttons: Array.from({ length: 17 }, (_, index) => ({ value: indices.includes(index) ? 1 : 0, pressed: indices.includes(index) })) })

describe('gamepad mapping', () => {
  it('gives the widgets of a kind the pad controls in layout order, and leaves other kinds alone', () => {
    const map = padSources([widget('s2', 'joystick', 8, 0), widget('s1', 'joystick', 0, 0), widget('k', 'keyboard', 0, 4), widget('d', 'display', 0, 8), widget('b2', 'button', 4, 4), widget('b1', 'button', 0, 8)])
    expect(Object.fromEntries(map)).toEqual({ s1: 'leftStick', s2: 'rightStick', k: 'dpad', b2: 'a', b1: 'b' })
  })

  it('stops giving controls when the pad has none left', () => {
    const map = padSources(['a', 'b', 'c'].map((id, index) => widget(id, 'joystick', index * 3, 0)))
    expect(map.has('c')).toBe(false)
  })

  it('reads the standard layout: sticks up positive, triggers, buttons and the D-pad', () => {
    const state = padStateOf(pressed([0, 7, 12, 15], [0.5, -1, 0, 0.25]))
    expect(state.leftStick).toEqual({ x: 0.5, y: 1 })
    expect(state.rightStick).toEqual({ x: 0, y: -0.25 })
    expect(state.dpad).toEqual({ x: 1, y: 1 })
    expect(state.buttons).toMatchObject({ a: 1, rightTrigger: 1, b: 0, leftTrigger: 0 })
  })

  it('keeps a stick inside the unit circle', () => {
    const corner = unitStick({ x: 1, y: 1 })
    expect(Math.hypot(corner.x, corner.y)).toBeCloseTo(1)
    expect(unitStick({ x: 0.3, y: 0.4 })).toEqual({ x: 0.3, y: 0.4 })
  })

  it('honours a control chosen by hand, and the others of the kind take what is left', () => {
    const map = padSources([{ ...widget('j1', 'joystick', 0, 0), pad: 'rightStick' }, widget('j2', 'joystick', 8, 0), { ...widget('j3', 'joystick', 16, 0), pad: 'off' }])
    expect(Object.fromEntries(map)).toEqual({ j1: 'rightStick', j2: 'leftStick' })
  })

  it('ignores a control the kind cannot use, and keeps the choice in the saved controller', () => {
    expect(padSources([{ ...widget('j', 'joystick', 0, 0), pad: 'a' }]).has('j')).toBe(false)
    const set = setPad({ widgets: [widget('s', 'slider', 0, 0)] }, 's', 'leftTrigger')
    expect(parseController(JSON.parse(JSON.stringify(set))).widgets[0]?.pad).toBe('leftTrigger')
    expect(setPad(set, 's', undefined).widgets[0]?.pad).toBeUndefined()
    expect(parseController({ widgets: [{ id: 's', kind: 'slider', pad: 'a' }] }).widgets[0]?.pad).toBeUndefined()
  })

  it('reads an Xbox pad the browser does not map: triggers and sticks on axes, the D-pad on a hat', () => {
    const id = 'Unknown Gamepad (Vendor: 045e Product: 0b13)'
    const axes = [0.5, -1, 0, -1, -1, 0, 0, 0, 0, 1.2857]
    const buttons = (down: number[]) => Array.from({ length: 16 }, (_, index) => ({ value: down.includes(index) ? 1 : 0, pressed: down.includes(index) }))
    const rest = padStateOf({ id, mapping: '', axes, buttons: buttons([]) })
    expect(rest.leftStick).toEqual({ x: 0.5, y: 1 })
    expect(rest.buttons).toMatchObject({ leftTrigger: 0, rightTrigger: 0, lb: 0 })
    expect(rest.dpad).toEqual({ x: 0, y: 0 })
    const held = padStateOf({ id, mapping: '', axes: axes.map((value, index) => (index === 3 ? 1 : index === 4 ? 0 : index === 9 ? -1 + 6 / 7 : value)), buttons: buttons([6, 3]) })
    expect(held.buttons).toMatchObject({ leftTrigger: 1, rightTrigger: 0.5, lb: 1, rb: 0, x: 1 })
    expect(held.dpad).toEqual({ x: 1, y: -1 })
  })
})
