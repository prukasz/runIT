import { describe, expect, it } from 'vitest'
import { loopBodyGate } from '../domain/canvas'
import { runitVmCatalog } from '../domain/descriptors'
import type { CanvasBlock } from '../domain/project'
import { canvasGroups } from './CanvasGroups'

const expr = (id: string, extra: Partial<CanvasBlock> = {}): CanvasBlock => ({ id, type: 'EXPR', x: 0, y: 0, expression: { code: ['in', 0] }, ...extra })
const summary = (blocks: readonly CanvasBlock[]) => canvasGroups({ id: 'c', name: 'c', blocks }, runitVmCatalog()).map((group) => [group.label, group.members.map((block) => block.id), group.depth])

describe('branch and loop areas', () => {
  it('groups the blocks of each gate, inherited ones included, and a loop with its body', () => {
    expect(summary([
      { id: 'if1', type: 'IF', x: 0, y: 0 },
      expr('scale', { inputs: [null], enables: [{ root: 'if1:q0' }] }),
      expr('use', { inputs: [{ root: 'scale:q0' }] }),
      expr('other', { inputs: [null], enables: [{ root: 'if1:q1' }] }),
      { id: 'loop1', type: 'FOR', x: 0, y: 0 },
      expr('step', { inputs: [{ root: 'loop1:q0' }], enables: [loopBodyGate('loop1')] }),
    // Areas of the same depth come in no particular order.
    ]).sort((a, b) => String(a[0]).localeCompare(String(b[0])))).toEqual([
      ['if1 · No', ['other'], 0],
      ['if1 · Yes', ['scale', 'use'], 0],
      ['loop1 · loop', ['loop1', 'step'], 0],
    ])
  })

  it('wraps a branch nested in a branch, outermost first', () => {
    expect(summary([
      { id: 'if1', type: 'IF', x: 0, y: 0 },
      { id: 'if2', type: 'IF', x: 0, y: 0, enables: [{ root: 'if1:q0' }] },
      expr('deep', { inputs: [null], enables: [{ root: 'if2:q0' }] }),
    ])).toEqual([
      ['if1 · Yes', ['if2', 'deep'], 1],
      ['if2 · Yes', ['deep'], 0],
    ])
  })
})
