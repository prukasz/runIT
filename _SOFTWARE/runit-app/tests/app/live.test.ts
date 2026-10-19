import { describe, expect, it } from 'vitest'
import type { TelemetryUpdate, TelemetryValue, WatchPlan } from '@engine'
import { LiveValues, liveText } from '../../src/app/run/live'

const plan: WatchPlan = { ids: [1, 2], targets: new Map([[1, ['b:eno']], [2, ['variable:x', 'b:out0']]]), dropped: 0 }
const value = (id: number, v: TelemetryValue['value'], type: TelemetryValue['type'] = 'bool', start = 0): TelemetryUpdate =>
  ({ values: [{ id, start, bytes: new Uint8Array(), type, value: v }], objects: [] })

describe('LiveValues', () => {
  it('keeps a pulse showing for the hold time after the flag went false', () => {
    const live = new LiveValues(300)
    live.apply(value(1, true), plan, 1000)
    expect(live.active('b:eno', 1000)).toBe(true)
    live.apply(value(1, false), plan, 1010)
    expect(live.active('b:eno', 1100)).toBe(true)
    expect(live.active('b:eno', 1311)).toBe(false)
    expect(live.nextExpiry(1100)).toBe(200)
    expect(live.nextExpiry(1400)).toBeUndefined()
  })

  it('counts a repeat of a true flag as a pulse but not a repeat of any other value', () => {
    const live = new LiveValues(300)
    expect(live.apply(value(1, true), plan, 0)).toBe(true)
    expect(live.apply(value(1, true), plan, 50)).toBe(true)
    expect(live.apply(value(2, 5, 'f32'), plan, 60)).toBe(true)
    expect(live.apply(value(2, 5, 'f32'), plan, 70)).toBe(false)
  })

  it('names one object by every target it stands for, and follows a change for the hold time', () => {
    const live = new LiveValues(200)
    live.apply(value(2, 2.5, 'f32'), plan, 0)
    expect(live.value('variable:x')).toBe(2.5)
    expect(live.value('b:out0')).toBe(2.5)
    expect(live.changed('variable:x', 150)).toBe(true)
    expect(live.changed('variable:x', 250)).toBe(false)
  })

  it("puts the part of a long value where it belongs, and ignores a folder's child ids", () => {
    const live = new LiveValues(200)
    live.apply(value(2, [1, 2, 3, 4], 'f32'), plan, 0)
    live.apply(value(2, [9, 9], 'f32', 2), plan, 5)
    expect(live.value('variable:x')).toEqual([1, 2, 9, 9])
    live.apply(value(2, [7, 8], 'ptr'), plan, 9)
    expect(live.value('variable:x')).toEqual([1, 2, 9, 9])
  })

  it('holds a boolean lit for the hold time after it was true, and says nothing of a number', () => {
    const live = new LiveValues(300)
    live.apply(value(1, true), plan, 0)
    live.apply(value(1, false), plan, 5)
    expect(live.flag('b:eno', 100)).toBe(true)
    expect(live.flag('b:eno', 400)).toBe(false)
    live.apply(value(2, 5, 'f32'), plan, 0)
    expect(live.flag('variable:x', 10)).toBeUndefined()
    expect(live.flag('nothing', 10)).toBeUndefined()
  })

  it('writes values as short texts', () => {
    expect(liveText(true)).toBe('true')
    expect(liveText(12.34567891)).toBe('12.346')
    expect(liveText([1, 2, 3, 4, 5])).toBe('[1, 2, 3, 4, …]')
    expect(liveText(Uint8Array.of(104, 105, 0, 0))).toBe('"hi"')
  })
})
