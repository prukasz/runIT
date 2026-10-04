import { describe, expect, it } from 'vitest'
import type { DevicePort } from '../../../src/domain/descriptors'
import { layoutFileName, resolveLayout, serializeLayout, variantOfFile } from '../../../src/domain/devices/componentLayout'

const ports: DevicePort[] = [
  { name: 'SDA', pin: 27, kind: 'bus', dir: 'inout' },
  { name: 'SCL', pin: 26, kind: 'bus', dir: 'in' },
  { name: 'VDD', pin: 28, kind: 'supply_in', dir: 'in', mV_min: 2300, mV_max: 5500 },
  { name: 'LED0', pin: 6, kind: 'io', dir: 'out', modes: ['BINARY', 'PWM'] },
]

const base = { image: 'breakout.png', size_cells: [8, 5] }

describe('component layout', () => {
  it('resolves placed pins and lists the rest as unplaced', () => {
    const { layout, problems } = resolveLayout({ ...base, anchors: { SDA: { x: 0.1, y: 0.4 }, LED0: { x: 0.9, y: 0.1 } } }, ports)
    expect(problems).toEqual([])
    expect(layout?.anchors.map((a) => a.port.name)).toEqual(['SDA', 'LED0'])
    expect(layout?.unplaced).toEqual(['SCL', 'VDD'])
    expect(layout?.anchors[1].modes).toEqual(['BINARY', 'PWM'])
  })

  it('applies per-variant electrical overrides over the csv', () => {
    const { layout } = resolveLayout({ ...base, anchors: { VDD: { x: 0, y: 0, mV_min: 3000, mV_max: 6000, dir: 'inout' } } }, ports)
    const vdd = layout?.anchors[0]
    expect([vdd?.mV_min, vdd?.mV_max, vdd?.dir]).toEqual([3000, 6000, 'inout'])
  })

  it('drops anchors the csv no longer has and reports them', () => {
    const { layout, problems } = resolveLayout({ ...base, anchors: { GONE: { x: 0.5, y: 0.5 }, SDA: { x: 0.5, y: 0.5 } } }, ports)
    expect(layout?.dropped).toEqual(['GONE'])
    expect(layout?.anchors).toHaveLength(1)
    expect(problems[0]).toContain('GONE')
  })

  it('skips an invalid anchor but keeps the layout', () => {
    const { layout, problems } = resolveLayout(
      { ...base, anchors: { SDA: { x: 1.5, y: 0 }, SCL: { x: 0, y: 0, mV_min: 6000, mV_max: 1000 }, LED0: { x: 0, y: 0, modes: ['PWM', 'FOO'] } } },
      ports,
    )
    expect(layout?.anchors).toEqual([])
    expect(problems).toHaveLength(3)
  })

  it('refuses a file without a usable picture, size or anchors', () => {
    expect(resolveLayout(null, ports).layout).toBeUndefined()
    expect(resolveLayout({ ...base, image: '../x.png', anchors: {} }, ports).layout).toBeUndefined()
    expect(resolveLayout({ ...base, size_cells: [0, 5], anchors: {} }, ports).layout).toBeUndefined()
    expect(resolveLayout({ ...base }, ports).layout).toBeUndefined()
  })

  it('names layout files per variant', () => {
    expect(layoutFileName('breakout')).toBe('app/canvas/breakout/layout.json')
    expect(variantOfFile('app/canvas/breakout/layout.json')).toBe('breakout')
    expect(variantOfFile('app/canvas/breakout/breakout.png')).toBeUndefined()
    expect(variantOfFile('device_pca9685.pins.csv')).toBeUndefined()
  })

  it('serializes in csv order with rounded positions and round-trips', () => {
    const text = serializeLayout({ ...base, size_cells: [8, 5], anchors: { LED0: { x: 0.123456, y: 0.5 }, SDA: { x: 0.1, y: 0.2 } } }, ports)
    expect(text.indexOf('"SDA"')).toBeLessThan(text.indexOf('"LED0"'))
    expect(text).toContain('0.1235')
    const again = resolveLayout(JSON.parse(text), ports)
    expect(again.problems).toEqual([])
    expect(again.layout?.anchors.map((a) => a.port.name)).toEqual(['SDA', 'LED0'])
  })
})

describe('editable layout', () => {
  it('keeps only overrides that differ from the csv', async () => {
    const { layoutOf } = await import('../../../src/domain/devices/componentLayout')
    const resolved = resolveLayout({ ...base, anchors: { VDD: { x: 0.1, y: 0.2, mV_max: 5500, mV_min: 3000 }, SDA: { x: 0.3, y: 0.4 } } }, ports).layout!
    const layout = layoutOf(resolved)
    expect(layout.anchors.VDD).toEqual({ x: 0.1, y: 0.2, mV_min: 3000 })
    expect(layout.anchors.SDA).toEqual({ x: 0.3, y: 0.4 })
  })

  it('sizes a picture in whole cells keeping its aspect', async () => {
    const { cellsFor } = await import('../../../src/domain/devices/componentLayout')
    expect(cellsFor(10, 400, 200)).toEqual([10, 5])
    expect(cellsFor(0, 400, 10)).toEqual([1, 1])
  })
})

describe('label angles', () => {
  it('a pin has its own quarter-turn angle, 0 when absent', () => {
    const { layout } = resolveLayout({ ...base, anchors: { SDA: { x: 0.1, y: 0.2 }, SCL: { x: 0.3, y: 0.4, angle: -90 } } }, ports)
    expect(layout?.anchors.map((a) => a.angle)).toEqual([0, -90])
  })

  it('refuses an angle that is not a quarter turn', () => {
    expect(resolveLayout({ ...base, anchors: { SDA: { x: 0, y: 0, angle: 45 } } }, ports).layout?.anchors).toEqual([])
    expect(resolveLayout({ ...base, anchors: { SDA: { x: 0, y: 0, angle: 270 } } }, ports).problems[0]).toContain('angle must be one of')
  })

  it('turns a quarter at a time and comes back round', async () => {
    const { nextLabelAngle } = await import('../../../src/domain/devices/componentLayout')
    expect([0, 90, 180, -90].map(nextLabelAngle)).toEqual([90, 180, -90, 0])
  })

  it('round-trips through the editable form, writing only a non-zero angle', async () => {
    const { layoutOf, serializeLayout: serialize } = await import('../../../src/domain/devices/componentLayout')
    const resolved = resolveLayout({ ...base, anchors: { SDA: { x: 0.1, y: 0.2 }, SCL: { x: 0.3, y: 0.4, angle: 90 } } }, ports).layout!
    const editable = layoutOf(resolved)
    expect(editable.anchors.SDA.angle).toBeUndefined()
    expect(editable.anchors.SCL.angle).toBe(90)
    const text = serialize(editable, ports)
    expect(text.match(/"angle"/g)).toHaveLength(1)
    expect(resolveLayout(JSON.parse(text), ports).layout?.anchors.map((a) => a.angle)).toEqual([0, 90])
  })
})
