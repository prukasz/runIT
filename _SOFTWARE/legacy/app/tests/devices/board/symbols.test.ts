import { describe, expect, it } from 'vitest'
import type { ProjectDevice } from '../../../src/domain/project'
import { runitDeviceCatalog } from '../../../src/domain/descriptors'
import { mirrored, placements, rotated, sizeMm, symbolOf, variantOf } from '../../../src/devices/board/symbols'

const pcaType = runitDeviceCatalog().type('device_pca9685')!

const dev = (id: string): ProjectDevice => ({ id, deviceId: 20, type: 'device_x', name: id, tags: [], install: {} })

describe('symbols', () => {
  it('uses the generated symbol of a type, scoped, with every pin', () => {
    const art = symbolOf(pcaType)
    expect(sizeMm(art)).toEqual({ width: 15.6, height: 25.2 })
    expect(art.markup.match(/<g id="pin-/g)).toHaveLength(28)
    const css = /<style>([\s\S]*?)<\/style>/.exec(art.markup)![1]!
    expect(css).toContain('.device-symbol .port:hover .terminal')
    expect(css).not.toMatch(/^\.body/m)
  })

  it('draws a breakout variant with its hidden pin greyed and without a terminal', () => {
    expect(pcaType.symbolVariants?.map((variant) => variant.id)).toEqual(['chip', 'breakout'])
    expect(variantOf(pcaType, undefined)?.id).toBe('chip')
    expect(variantOf(pcaType, 'nope')?.id).toBe('chip')
    const chip = symbolOf(pcaType, 'chip')
    const breakout = symbolOf(pcaType, 'breakout')
    expect(chip.markup).toContain('id="pin-EXTCLK-t"')
    expect(breakout.markup).not.toContain('id="pin-EXTCLK-t"')
    expect(breakout.markup).toContain('is-hidden')
    expect(breakout.markup.match(/<g id="pin-/g)).toHaveLength(28)
  })

  it('gives a type without a symbol a plain box with its name', () => {
    const art = symbolOf({ id: 'device_unknown', title: 'Mystery <1>' })
    expect(art.markup).toContain('Mystery &#60;1&#62;')
  })

  it('stacks unplaced devices right of the board; a placed one does not move the others', () => {
    const size = () => ({ width: 30, height: 40 })
    const open = placements([dev('a'), dev('b')], undefined, 100, size)
    expect(open.get('a')).toMatchObject({ x: 110, y: 0 })
    expect(open.get('b')).toMatchObject({ x: 110, y: 50 })
    const one = placements([dev('a'), dev('b')], { a: { x: 3, y: 4, rotation: 0, mirror: false } }, 100, size)
    expect(one.get('a')).toMatchObject({ x: 3, y: 4 })
    expect(one.get('b')).toMatchObject({ x: 110, y: 0 })
  })

  it('rotates in quarter turns and mirrors', () => {
    const p = { x: 0, y: 0, rotation: 270 as const, mirror: false }
    expect(rotated(p).rotation).toBe(0)
    expect(mirrored(p).mirror).toBe(true)
  })
})
