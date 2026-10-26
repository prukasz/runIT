import { describe, expect, it } from 'vitest'
import { bundledBoard, bundledServiceCatalog } from '../../src/backend'
import { deviceList } from '../../src/app/devices/devices'
import { emptyProject } from '../../src/app/project/projectFile'
import { buildSvg, draftProblems, newConnector, newDraft, partFiles, roleChoices, roleOf, slug, withPinCount, withRole } from '../../src/app/hardware/creator'
import { connectorPinPoints } from '../../src/app/hardware/geometry'
import { maskOf, simplify, vectorize } from '../../src/app/hardware/imageTrace'
import type { Pixels } from '../../src/app/hardware/imageTrace'
import { bundledLibrary } from '../../src/app/hardware/library'

const catalog = bundledServiceCatalog()
const board = bundledBoard('pcb')
const library = bundledLibrary()

describe('the part creator', () => {
  it('names the folder from the name', () => {
    expect(slug('PWM Breakout (PCA9685) #2')).toBe('pwm_breakout_pca9685_2')
  })

  it('makes connectors at the standard pitch, with as many pins as asked', () => {
    const draft = newDraft()
    const header = withPinCount(newConnector(draft.part, 'goldpin', { x: 2.54, y: 2.54 }), 6)
    expect(header.pins).toHaveLength(6)
    const points = connectorPinPoints(header)
    expect(points[5]!.x - points[0]!.x).toBeCloseTo(5 * 2.54)
    const screw = newConnector(draft.part, 'screw_terminal', { x: 0, y: 0 })
    expect(connectorPinPoints(screw)[1]!.x).toBeCloseTo(5)
  })

  it('turns a pin role into its fields and back', () => {
    const pin = { id: 'p1', label: '1', nc: true as const }
    for (const role of ['gnd', 'net:vcc', 'self:3', 'dev:3:5', 'config:pwm_pin', 'bus:i2c:sda', 'motor:b', 'hb:5:1:vm1', 'nc']) {
      expect(roleOf(withRole(pin, role))).toBe(role)
    }
    expect(withRole(pin, 'gnd')).toEqual({ id: 'p1', label: '1', gnd: true })
  })

  it('offers the pins and the pin fields of the part\'s device, and on a board the board\'s pins', () => {
    const servo = catalog.devices.get('servo')
    const choices = roleChoices({ ...newDraft().part, device: { type: 'servo' } }, servo, [])
    expect(choices.map((c) => c.value)).toContain('config:pwm_pin')
    const pca = roleChoices(newDraft().part, catalog.devices.get('pca9685'), [])
    expect(pca.filter((c) => c.value.startsWith('self:'))).toHaveLength(16)
    const boardDevices = deviceList(emptyProject(), catalog, board).filter((d) => d.system)
    const onBoard = roleChoices({ ...newDraft().part, category: 'board' }, undefined, boardDevices)
    expect(onBoard.map((c) => c.value)).toEqual(expect.arrayContaining(['dev:0:18', 'hb:5:0:']))
  })

  it('saves part.json and part.svg the library checks', () => {
    const draft = newDraft()
    const files = partFiles({ ...draft, part: { ...draft.part, id: 'my_breakout', connectors: [withPinCount(newConnector(draft.part, 'goldpin', { x: 2.54, y: 2.54 }), 4)] } })
    const json = JSON.parse(files.json)
    expect(json).toMatchObject({ kind: 'part', id: 'my_breakout', graphic: 'part.svg', size_mm: { w: 40, h: 25 } })
    expect(files.svg).toContain('viewBox="0 0 40 25"')
    expect(buildSvg(draft)).toContain('<rect')
    expect(draftProblems({ ...draft, part: { ...draft.part, id: 'servo_sg90' } }, library)).toHaveLength(1)
  })
})

/** A white picture with a red block (10..29 × 5..24) and a blue square in it. */
const picture = (): Pixels => {
  const [w, h] = [40, 30]
  const data = new Uint8ClampedArray(w * h * 4).fill(255)
  for (let y = 5; y < 25; y++) for (let x = 10; x < 30; x++) {
    const blue = x >= 15 && x < 22 && y >= 10 && y < 17
    data.set(blue ? [20, 40, 220, 255] : [220, 30, 30, 255], (y * w + x) * 4)
  }
  return { width: w, height: h, data }
}

describe('a photo into a drawing', () => {
  it('takes the background away from the corners and keeps the part', () => {
    const mask = maskOf(picture(), [{ x: 0, y: 0 }], 30)
    expect(mask[0]).toBe(0)
    expect(mask[12 * 40 + 12]).toBe(1)
    expect(mask.reduce((n, v) => n + v, 0)).toBe(20 * 20)
  })

  it('traces it at the real size, in its colours', () => {
    const img = picture()
    const traced = vectorize(img, maskOf(img, [{ x: 0, y: 0 }], 30), { colours: 2, width_mm: 20, detail: 2 })!
    expect(traced.w_mm).toBe(20)
    expect(traced.h_mm).toBe(20)
    expect(traced.svg.match(/<path/g)?.length).toBeGreaterThanOrEqual(2)
    expect(traced.svg).toContain('viewBox="0 0 20 20"')
  })

  it('simplifies a straight run to its ends', () => {
    const line = Array.from({ length: 20 }, (_, i) => ({ x: i, y: 0 }))
    const ring = [...line, { x: 19, y: 10 }, { x: 0, y: 10 }]
    expect(simplify(ring, 0.5).length).toBeLessThanOrEqual(5)
  })
})
