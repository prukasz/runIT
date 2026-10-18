import { describe, expect, it } from 'vitest'
import { clampWidth, COLLAPSED_WIDTH, FRAME_SPACE, MAX_PANEL, MAX_RIGHT_PANEL, MIN_CENTER, MIN_PANEL } from '@ui/layout/panelWidth'

describe('the width of a side panel', () => {
  it('is what was asked for, inside the limits', () => {
    expect(clampWidth(300, COLLAPSED_WIDTH, 1920)).toBe(300)
  })

  it('is never narrower than the floor or wider than the ceiling', () => {
    expect(clampWidth(50, COLLAPSED_WIDTH, 1920)).toBe(MIN_PANEL)
    expect(clampWidth(900, COLLAPSED_WIDTH, 1920)).toBe(MAX_PANEL)
    expect(clampWidth(900, COLLAPSED_WIDTH, 1920, MAX_RIGHT_PANEL)).toBe(MAX_RIGHT_PANEL)
  })

  it('leaves the centre column its minimum, given the other panel and the window', () => {
    const view = 1000
    const other = 300
    expect(clampWidth(500, other, view)).toBe(view - other - MIN_CENTER - FRAME_SPACE)
  })

  it('keeps the floor in a window too small for everything', () => {
    expect(clampWidth(300, 300, 600)).toBe(MIN_PANEL)
  })

  it('is a whole number of pixels', () => {
    expect(Number.isInteger(clampWidth(301.7, COLLAPSED_WIDTH, 1920))).toBe(true)
  })
})
