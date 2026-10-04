import { describe, expect, it } from 'vitest'
import { runitDeviceCatalog } from '../../../src/domain/descriptors'
import { resolveLayout } from '../../../src/domain/devices/componentLayout'
import { pictureArt, variantsOf } from '../../../src/devices/board/pictureSymbols'
import { sizeMm } from '../../../src/devices/board/symbols'

const pcaType = runitDeviceCatalog().type('device_pca9685')!

describe('picture symbols', () => {
  const { layout } = resolveLayout({ image: 'x.png', size_cells: [20, 10], anchors: { SDA: { x: 0.5, y: 0.25 }, VDD: { x: 0, y: 1, mV_min: 3000, mV_max: 5500 } } }, pcaType.ports)

  it('draws the picture at the layout size with a terminal per placed pin', () => {
    const art = pictureArt(layout!, '/img/x.png')
    expect(sizeMm(art)).toEqual({ width: 20, height: 10 })
    expect(art.markup).toContain('<image href="/img/x.png"')
    expect(art.markup.match(/<g id="pin-/g)).toHaveLength(2)
    expect(art.markup).toContain('id="pin-SDA-t" cx="100.0" cy="25.0"')
    expect(art.markup).toContain('3000–5500 mV')
  })

  it('lists the generated variants of a device that has no saved pictures', () => {
    expect(variantsOf(pcaType).filter((variant) => !variant.picture).map((variant) => variant.id)).toEqual(['chip', 'breakout'])
    expect(variantsOf(undefined)).toEqual([])
  })
})

describe('picture symbol labels', () => {
  it('turns a pin name about its anchor by that pin\'s own angle only', () => {
    const { layout } = resolveLayout({ image: 'x.png', size_cells: [20, 10], anchors: { SDA: { x: 0.5, y: 0.5, angle: -90 }, SCL: { x: 0.25, y: 0.5 } } }, pcaType.ports)
    const markup = pictureArt(layout!, '/img/x.png').markup
    expect(markup).toContain('transform="rotate(-90 100.0 50.0)"')
    expect(markup.match(/transform="rotate/g)).toHaveLength(1)
  })
})
