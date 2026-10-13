import { describe, expect, it } from 'vitest'
import { BLOCKS } from '../../src/backend/vm/blocks'
import { blockShape, decodePlacement, descriptorOf, encodePlacement, headlineOf, pinAnchor, typeKeyOf } from '../../src/blocks/blockModel'
import { addBlock, addCanvas, connect, disconnect, findBlock, moveBlock, newBlockId, removeBlock, sourcePath, wiresOf } from '../../src/domain/canvas'

describe('block shapes', () => {
  it('every descriptor has a shape on the grid with its pins', () => {
    for (const descriptor of BLOCKS.values()) {
      const key = typeKeyOf(descriptor)
      expect(descriptorOf(key)).toBe(descriptor)
      const shape = blockShape(descriptor, { id: 'x', type: key })
      expect(shape.width % 20).toBe(0)
      expect(shape.height).toBeGreaterThanOrEqual(60)
      expect(shape.inputs.length).toBeGreaterThanOrEqual(descriptor.inputs?.min ?? 0)
      expect(shape.outputs.length).toBeGreaterThanOrEqual(descriptor.outputs?.min ?? 0)
    }
  })

  it('an unknown type still draws', () => {
    expect(blockShape(undefined, { id: 'x', type: 'NOPE' }).width).toBeGreaterThan(0)
  })
})

describe('canvas editing', () => {
  const key = typeKeyOf([...BLOCKS.values()].find((d) => (d.outputs?.pins.length ?? 0) > 0 && (d.inputs?.pins.length ?? 0) > 0)!)

  it('places, wires, moves and removes blocks', () => {
    let canvases = addCanvas([], 'c1')
    const a = newBlockId(canvases, key)
    canvases = addBlock(canvases, 'c1', { id: a, type: key, x: 0, y: 0 })
    const b = newBlockId(canvases, key)
    canvases = addBlock(canvases, 'c1', { id: b, type: key, x: 200, y: 0 })
    expect(a).not.toBe(b)

    const wired = connect(findBlock(canvases, b)!.block, { block: b, kind: 'in', index: 0 }, sourcePath({ block: a, pin: 'q0' }))
    canvases = canvases.map((canvas) => ({ ...canvas, blocks: canvas.blocks.map((block) => (block.id === b ? wired : block)) }))
    expect(wiresOf(canvases[0]!)).toEqual([{ from: { block: a, pin: 'q0' }, to: { block: b, kind: 'in', index: 0 } }])

    const gated = connect(findBlock(canvases, b)!.block, { block: b, kind: 'en' }, sourcePath({ block: a, pin: 'eno' }))
    expect(gated.enables).toHaveLength(1)
    expect(disconnect(gated, { block: b, kind: 'en' }, 0).enables).toBeUndefined()

    canvases = moveBlock(canvases, a, { x: 40, y: 60 })
    expect(findBlock(canvases, a)!.block).toMatchObject({ x: 40, y: 60 })
    canvases = removeBlock(canvases, a)
    expect(findBlock(canvases, a)).toBeUndefined()
  })

  it('anchors wires on the pin rows', () => {
    const descriptor = descriptorOf(key)!
    const shape = blockShape(descriptor, { id: 'x', type: key })
    const input = pinAnchor({ x: 100, y: 100 }, shape, { side: 'in', index: 0 })
    const output = pinAnchor({ x: 100, y: 100 }, shape, { side: 'out', index: 0 })
    expect(input.x).toBe(100)
    expect(output.x).toBe(100 + shape.width)
    expect(input.y).toBeGreaterThan(100)
  })
})

describe('device blocks', () => {
  const servo = [...BLOCKS.values()].find((d) => d.alias === 'Servo')!
  const key = typeKeyOf(servo)

  it('have no pins until an operation is chosen, then the operation decides them', () => {
    expect(blockShape(servo, { id: 's', type: key }).inputs).toHaveLength(0)
    const position = servo.ops!.find((op) => op.alias === 'Position')!
    const shape = blockShape(servo, { id: 's', type: key, settings: { op: position.value } })
    expect(shape.inputs.map((pin) => pin.title.split(' ')[0])).toEqual(['Position'])
    const home = servo.ops!.find((op) => op.alias === 'Home')!
    expect(blockShape(servo, { id: 's', type: key, settings: { op: home.value } }).inputs).toHaveLength(0)
  })

  it('say what they do and with which device', () => {
    expect(headlineOf(servo, { id: 's', type: key })).toEqual({ lead: 'Servo', value: 'no device' })
    const position = servo.ops!.find((op) => op.alias === 'Position')!
    expect(headlineOf(servo, { id: 's', type: key, settings: { op: position.value, device_id: 3 } })).toEqual({ lead: 'Move to Position', value: '3' })
  })

  it('are placed with their operation from a palette drag', () => {
    const op = servo.ops![0]!
    expect(decodePlacement(encodePlacement(key, op.value))).toEqual({ typeKey: key, settings: { op: op.value } })
    expect(decodePlacement(encodePlacement('EXPR'))).toEqual({ typeKey: 'EXPR' })
  })
})
