import { describe, expect, it } from 'vitest'
import { snapAnchor } from '../../../src/domain/devices/anchorSnap'

const reach = { thresholdX: 0.01, thresholdY: 0.01, sizeCells: [60, 25] as const }
const row = [{ x: 0.20, y: 0.70 }, { x: 0.24, y: 0.70 }, { x: 0.28, y: 0.70 }]

describe('anchor snap', () => {
  it('off leaves the point alone', () => {
    expect(snapAnchor({ x: 0.123, y: 0.456 }, row, { ...reach, mode: 'off' })).toEqual({ x: 0.123, y: 0.456 })
  })

  it('lines a pin up with the row of a pin within reach', () => {
    const snapped = snapAnchor({ x: 0.5, y: 0.705 }, row, { ...reach, mode: 'pins' })
    expect(snapped.y).toBe(0.70)
    expect(snapped.guideY).toBe(0.70)
    expect(snapped.x).toBe(0.5)
  })

  it('takes the pitch of the pins already on the row', () => {
    const snapped = snapAnchor({ x: 0.318, y: 0.702 }, row, { ...reach, mode: 'pins' })
    expect(snapped.y).toBe(0.70)
    expect(snapped.x).toBeCloseTo(0.32, 6)
  })

  it('counts pins placed by hand a little off the row as on it, for the pitch', () => {
    const rough = [{ x: 0.20, y: 0.700 }, { x: 0.24, y: 0.706 }]
    const snapped = snapAnchor({ x: 0.275, y: 0.702 }, rough, { ...reach, mode: 'pins' })
    expect(snapped.y).toBe(0.7)
    expect(snapped.x).toBeCloseTo(0.28, 6)
  })

  it('lines a pin up with a column and keeps its row free', () => {
    const snapped = snapAnchor({ x: 0.243, y: 0.2 }, row, { ...reach, mode: 'pins' })
    expect(snapped.x).toBe(0.24)
    expect(snapped.guideX).toBe(0.24)
    expect(snapped.guideY).toBeUndefined()
    expect(snapped.y).toBe(0.2)
  })

  it('does not snap to a pin out of reach', () => {
    expect(snapAnchor({ x: 0.6, y: 0.3 }, row, { ...reach, mode: 'pins' })).toEqual({ x: 0.6, y: 0.3 })
  })

  it('a grid snaps to whole steps of millimetres from the top-left corner', () => {
    // 60 x 25 mm picture, 2.54 mm grid: x 0.5 = 30 mm -> 12 steps = 30.48 mm; y 0.5 = 12.5 mm -> 5 steps = 12.7 mm.
    const snapped = snapAnchor({ x: 0.5, y: 0.5 }, [], { ...reach, mode: 2.54 })
    expect(snapped.x).toBeCloseTo(30.48 / 60, 6)
    expect(snapped.y).toBeCloseTo(12.7 / 25, 6)
  })

  it('keeps a grid snap inside the picture', () => {
    const snapped = snapAnchor({ x: 1, y: 0 }, [], { ...reach, mode: 7 })
    expect(snapped.x).toBeLessThanOrEqual(1)
    expect(snapped.y).toBe(0)
  })
})
