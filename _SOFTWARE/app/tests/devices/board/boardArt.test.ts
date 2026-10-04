import { describe, expect, it } from 'vitest'
import { readFileSync } from 'node:fs'
import { GRID } from '../../../src/domain/canvas'
import { ART_SCALE, fitViewport, scopeSvg } from '../../../src/devices/board/boardArt'

const raw = readFileSync('src/devices/board/runit-board.svg', 'utf8')

describe('board art', () => {
  const art = scopeSvg(raw, 'board-art')

  it('reads the size from the viewBox', () => {
    expect([art.width, art.height]).toEqual([616, 1000])
  })

  it('scopes every style rule so the art cannot restyle the app', () => {
    const css = /<style>([\s\S]*?)<\/style>/.exec(art.markup)![1]
    const rules = css.split('\n').filter((line) => line.includes('{'))
    expect(rules.length).toBeGreaterThan(10)
    for (const rule of rules) expect(rule.trim().startsWith('.board-art ')).toBe(true)
  })

  it('keeps the terminal groups', () => {
    for (const id of ['ain0', 'i2c-sda', 'usbc', 'xt60', 'pwm1-pwm']) expect(art.markup).toContain(`<g id="${id}"`)
  })

  it('fits the board centred and never above 100%', () => {
    const view = fitViewport(art, { width: 2000, height: 3000 })
    expect(view.zoom).toBe(1)
    expect(view.x).toBe((2000 - 616) / 2)
    const small = fitViewport(art, { width: 400, height: 500 })
    expect(small.zoom).toBeLessThan(0.5)
    expect(small.zoom * art.height).toBeLessThanOrEqual(500)
  })
})

describe('board scale', () => {
  it('shows one grid cell as one millimetre', () => {
    // The runIT art is 616 × 1000 units at 20 units per mm: a 30.8 × 50 mm board.
    expect(ART_SCALE).toBe(1)
    expect(616 * ART_SCALE / GRID).toBeCloseTo(30.8)
  })
})
