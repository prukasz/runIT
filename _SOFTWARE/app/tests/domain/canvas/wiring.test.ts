import { describe, expect, it } from 'vitest'
import type { CanvasBlock, ProjectCanvas } from '../../../src/domain/project'
import { accepts, connect, disconnect, sourcePath, variableKind, wiresOf } from '../../../src/domain/canvas'

const block = (id: string, extra: Partial<CanvasBlock> = {}): CanvasBlock => ({ id, type: 'EXPR', x: 0, y: 0, ...extra })

describe('wiring', () => {
  it('stores a wire as the input path and an EN wire as an enable, once', () => {
    let target = connect(block('b'), { block: 'b', kind: 'in', index: 1 }, sourcePath({ block: 'a', pin: 'q0' }))
    expect(target.inputs).toEqual([null, { root: 'a:q0' }])
    target = connect(target, { block: 'b', kind: 'en' }, sourcePath({ block: 'if1', pin: 'q0' }))
    expect(connect(target, { block: 'b', kind: 'en' }, { root: 'if1:q0' }).enables).toEqual([{ root: 'if1:q0' }])
    expect(connect(target, { block: 'b', kind: 'out', index: 0 }, { root: 'result' }).outputs).toEqual(['result'])
    expect(disconnect(target, { block: 'b', kind: 'en' }, 0)).not.toHaveProperty('enables')
    expect(disconnect(target, { block: 'b', kind: 'in', index: 1 }).inputs).toEqual([null, null])
  })

  it('lists the block-to-block wires of a canvas, not the variables', () => {
    const canvas: ProjectCanvas = { id: 'c', name: 'c', blocks: [block('a'), block('b', { inputs: [{ root: 'a:q0' }, { root: 'count' }], enables: [{ root: 'a:eno' }, { root: 'gone:q0' }] })] }
    expect(wiresOf(canvas)).toEqual([
      { from: { block: 'a', pin: 'q0' }, to: { block: 'b', kind: 'in', index: 0 } },
      { from: { block: 'a', pin: 'eno' }, to: { block: 'b', kind: 'en' }, enableIndex: 0 },
    ])
  })

  it('lets numbers and bools meet, keeps strings and objects to object pins, a loop body to EN', () => {
    expect(accepts('f32', 'bool')).toBe(true)
    expect(accepts('en', 'number')).toBe(true)
    expect(accepts('f32', 'string')).toBe(false)
    expect(accepts('object', 'string')).toBe(true)
    expect(accepts('ptr-cell', 'object')).toBe(true)
    expect(accepts('ptr-cell', 'number', { fromBlock: true })).toBe(false)
    expect(accepts('f32', 'bool', { body: true })).toBe(false)
    expect(accepts('en', 'bool', { body: true })).toBe(true)
    expect(accepts('u32', 'any')).toBe(true)
    expect(variableKind({ kind: 'value', id: 'b', name: 'b', type: 'B', length: 1, mutable: true, retentive: false })).toBe('bool')
    expect(variableKind({ kind: 'folder', id: 'f', name: 'f', children: [] })).toBe('object')
  })
})
