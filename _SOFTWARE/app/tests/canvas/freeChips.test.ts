import { describe, expect, it } from 'vitest'
import { addBlock, addCanvas, addFreeChip, connect, dockFreeChip, findBlock, freeChipFor, moveFreeChip, pinPath, removeFreeChip, wiresOf } from '../../src/domain/canvas'
import type { ProjectCanvas } from '../../src/domain/canvas'

const base = (): ProjectCanvas => {
  const canvases = addBlock(addCanvas([], 'c1'), 'c1', { id: 'expr1', type: 'EXPR', x: 0, y: 0, inputs: [{ root: 'speed' }], outputs: ['result'] })
  return addBlock(canvases, 'c1', { id: 'expr2', type: 'EXPR', x: 200, y: 0 })[0]!
}

describe('free chips', () => {
  it('adds one with its own ID and place, and removes it again', () => {
    const one = addFreeChip(base(), { root: 'speed' }, { x: 40, y: 60 })
    expect(one.variables).toEqual([{ id: 'var1', path: { root: 'speed' }, x: 40, y: 60 }])
    const two = addFreeChip(one, { root: 'speed' }, { x: 0, y: 0 })
    expect(two.variables?.map((entry) => entry.id)).toEqual(['var1', 'var2'])
    expect(removeFreeChip(removeFreeChip(two, 'var1'), 'var2').variables).toBeUndefined()
  })

  it('leaves the pins as they were: undocking changes nothing the program reads', () => {
    const canvas = base()
    const undocked = addFreeChip(canvas, { root: 'speed' }, { x: 40, y: 60 })
    expect(undocked.blocks).toBe(canvas.blocks)
    expect(wiresOf(undocked)).toEqual(wiresOf(canvas))
  })

  it('finds the chip a pin is wired to by its path, steps included', () => {
    const canvas = addFreeChip(addFreeChip(base(), { root: 'speed' }, { x: 0, y: 0 }), { root: 'gains', steps: [{ kind: 'index', index: 2 }] }, { x: 0, y: 40 })
    expect(freeChipFor(canvas, { root: 'speed' })?.id).toBe('var1')
    expect(freeChipFor(canvas, { root: 'gains', steps: [{ kind: 'index', index: 2 }] })?.id).toBe('var2')
    expect(freeChipFor(canvas, { root: 'gains' })).toBeUndefined()
  })

  it('reads the path off an input, an output or an enable', () => {
    const block = base().blocks[0]!
    expect(pinPath(block, { block: 'expr1', kind: 'in', index: 0 })).toEqual({ root: 'speed' })
    expect(pinPath(block, { block: 'expr1', kind: 'out', index: 0 })).toEqual({ root: 'result' })
    expect(pinPath(block, { block: 'expr1', kind: 'in', index: 3 })).toBeUndefined()
  })

  it('moves a chip', () => {
    const moved = moveFreeChip(addFreeChip(base(), { root: 'speed' }, { x: 0, y: 0 }), 'var1', { x: 80, y: 20 })
    expect(moved.variables?.[0]).toMatchObject({ x: 80, y: 20 })
  })

  it('docks a chip on a pin: the pin takes the path and the chip goes', () => {
    const chip = addFreeChip(base(), { root: 'speed' }, { x: 0, y: 0 })
    const docked = dockFreeChip(chip, 'var1', 'expr2', { block: 'expr2', kind: 'in', index: 0 })
    expect(docked.variables).toBeUndefined()
    expect(docked.blocks.find((block) => block.id === 'expr2')?.inputs?.[0]).toEqual({ root: 'speed' })
    expect(findBlock([docked], 'expr1')?.block.inputs?.[0]).toEqual({ root: 'speed' })
  })

  it('docking a chip that is gone changes nothing', () => {
    const canvas = base()
    expect(dockFreeChip(canvas, 'nope', 'expr2', { block: 'expr2', kind: 'in', index: 0 })).toBe(canvas)
    expect(connect(canvas.blocks[1]!, { block: 'expr2', kind: 'in', index: 0 }, { root: 'x' }).inputs?.[0]).toEqual({ root: 'x' })
  })
})
