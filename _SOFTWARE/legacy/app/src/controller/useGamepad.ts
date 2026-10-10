import { useEffect, useRef } from 'react'
import { axisValue, flagOption, lerp, numberOption, padSources, padStateOf, stepped, unitStick } from '../domain/controller'
import type { Controller, ControllerWidget, PadSource, PadState } from '../domain/controller'
import type { WidgetIo } from './widgets'

const PRESSED = 0.5
const TRIGGER_NOISE = 0.03
const STICK_NOISE = 0.08
const stickAxisOf = (source: PadSource, pad: PadState): number | undefined =>
  source === 'leftStickX' ? pad.leftStick.x : source === 'leftStickY' ? pad.leftStick.y : source === 'rightStickX' ? pad.rightStick.x : source === 'rightStickY' ? pad.rightStick.y : undefined
/** LT / RT pull proportionally (0..1); LB / RB are buttons, so all or nothing. */
const triggerOf = (source: PadSource, pad: PadState): number => {
  const pulled = source === 'leftTrigger' ? pad.buttons.leftTrigger : source === 'rightTrigger' ? pad.buttons.rightTrigger : source === 'lb' ? pad.buttons.lb : source === 'rb' ? pad.buttons.rb : 0
  if (source === 'lb' || source === 'rb') return pulled > PRESSED ? 1 : 0
  return pulled < TRIGGER_NOISE ? 0 : pulled
}
const leaned = (axis: number): number => (axis > PRESSED ? 1 : axis < -PRESSED ? -1 : 0)

/** What a pad control gives the widget: the value per slot, and what the slot returns to when the pad lets go (if anything). */
type Output = readonly { readonly slot: string; readonly value: number; readonly rest?: number }[]

const outputOf = (widget: ControllerWidget, source: PadSource, pad: PadState): Output => {
  switch (widget.kind) {
    case 'joystick': {
      const stick = unitStick(source === 'rightStick' ? pad.rightStick : pad.leftStick)
      const min = numberOption(widget, 'min')
      const max = numberOption(widget, 'max')
      const dead = numberOption(widget, 'deadzone') / 100
      const rest = lerp(min, max, 0.5)
      return [
        { slot: 'x', value: axisValue(stick.x, min, max, dead, flagOption(widget, 'invertX')), rest },
        { slot: 'y', value: axisValue(stick.y, min, max, dead, flagOption(widget, 'invertY')), rest },
      ]
    }
    case 'keyboard': {
      const amount = numberOption(widget, 'amount')
      const rest = numberOption(widget, 'rest')
      // A stick works as the arrow keys do: past half way is pressed.
      const stick = source === 'rightStick' ? pad.rightStick : pad.leftStick
      const direction = source === 'dpad' ? pad.dpad : { x: leaned(stick.x), y: leaned(stick.y) }
      return [{ slot: 'x', value: rest + direction.x * amount, rest }, { slot: 'y', value: rest + direction.y * amount, rest }]
    }
    case 'slider': {
      const min = numberOption(widget, 'min')
      const max = numberOption(widget, 'max')
      const stickAxis = stickAxisOf(source, pad)
      // A trigger (or bumper) pulls from the lowest value to the highest; a stick axis runs through the middle (its centre is the middle value).
      const fraction = stickAxis !== undefined ? (Math.abs(stickAxis) < STICK_NOISE ? 0.5 : (stickAxis + 1) / 2) : triggerOf(source, pad)
      return [{ slot: 'value', value: stepped(lerp(min, max, fraction), min, max, numberOption(widget, 'step')) }]
    }
    case 'button': {
      const release = numberOption(widget, 'releaseValue')
      return [{ slot: 'value', value: pad.buttons[source as 'a'] > PRESSED ? numberOption(widget, 'pressValue') : release, rest: release }]
    }
    default:
      return []
  }
}

const firstPad = (): Gamepad | undefined => {
  if (typeof navigator === 'undefined' || !navigator.getGamepads) return undefined
  const pads = [...navigator.getGamepads()].filter((pad): pad is Gamepad => !!pad && pad.connected)
  return pads.find((pad) => pad.mapping === 'standard') ?? pads[0]
}

/**
 * Drives the widgets from a gamepad while the interface runs (domain/controller/gamepad.ts says which control goes
 * where). A value is written only when it changes, and only once the control has moved from where it was first
 * seen, so enabling the interface does not send the resting pad to the board. When the pad goes away, or the
 * interface stops, what the pad held returns to its rest value.
 */
export function useGamepad(controller: Controller, io: WidgetIo, active: boolean): void {
  const latest = useRef({ controller, io })
  latest.current = { controller, io }
  useEffect(() => {
    if (!active) return undefined
    const first = new Map<string, number>()
    const sent = new Map<string, { value: number; rest?: number; variable: string | undefined }>()
    const wasDown = new Map<string, boolean>()
    let present = false
    let frame = 0

    const letGo = (): void => {
      for (const entry of sent.values()) if (entry.rest !== undefined && entry.value !== entry.rest) latest.current.io.write(entry.variable, entry.rest)
      sent.clear()
      first.clear()
      wasDown.clear()
    }
    const tick = (): void => {
      frame = requestAnimationFrame(tick)
      const raw = firstPad()
      if (!raw) {
        if (present) { present = false; letGo() }
        return
      }
      present = true
      const pad = padStateOf(raw)
      const { controller: current, io: link } = latest.current
      for (const [id, source] of padSources(current.widgets)) {
        const widget = current.widgets.find((entry) => entry.id === id)
        if (!widget) continue
        if (widget.kind === 'switch') {
          const down = pad.buttons[source as 'lb'] > PRESSED
          const before = wasDown.get(id)
          wasDown.set(id, down)
          if (before === false && down) {
            const on = numberOption(widget, 'onValue')
            link.write(widget.links.value, link.read(widget.links.value) === on ? numberOption(widget, 'offValue') : on)
          }
          continue
        }
        for (const out of outputOf(widget, source, pad)) {
          const key = `${id}:${out.slot}`
          const seen = first.get(key)
          if (seen === undefined) { first.set(key, out.value); continue }
          const last = sent.get(key)
          if (last ? last.value === out.value : out.value === seen) continue
          sent.set(key, { value: out.value, ...(out.rest !== undefined ? { rest: out.rest } : {}), variable: widget.links[out.slot] })
          link.write(widget.links[out.slot], out.value)
        }
      }
    }
    frame = requestAnimationFrame(tick)
    return () => {
      cancelAnimationFrame(frame)
      letGo()
    }
  }, [active])
}
