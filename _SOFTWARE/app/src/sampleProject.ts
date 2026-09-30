import { recoveredCanvases } from './canvas'
import { createProject } from './domain/project'
import type { ObjectNode, ObjectPath, ProgramBlock, ProjectDocument, ValueNode } from './domain/project'

/*
 * A small working program to try the app with: a counter fed by a periodic
 * tick, a table lookup by a variable position, an alarm with a delay, an edge
 * latch, a switch and a loop. The same blocks the board sample test runs
 * (compiler/programSamples.test.ts).
 */

const value = (id: string, type: string, initial?: ValueNode['value'], length = 1, subscribed = false): ValueNode => ({ kind: 'value', id, name: id, type, length, ...(initial ? { value: initial } : {}), mutable: true, retentive: false, ...(subscribed ? { subscribed } : {}) })
const at = (root: string): ObjectPath => ({ root })
const tick = at('every:eno')

const objects: readonly ObjectNode[] = [
  value('count', 'F', undefined, 1, true),
  value('sel', 'U8', [2]),
  value('table', 'U32', [100, 200, 300, 400], 4),
  value('copy', 'F', undefined, 1, true),
  value('acc', 'F', undefined, 1, true),
]

const blocks: readonly ProgramBlock[] = [
  { id: 'every', type: 'PERIODIC', settings: { period: 100, time_base: 'MS' } },
  { id: 'counter', type: 'EXPR', inputs: [at('count')], outputs: ['count'], enables: [tick], expression: { constants: [1], code: ['in', 0, 'const', 0, '+'] } },
  { id: 'pick', type: 'EXPR', inputs: [{ root: 'table', steps: [{ kind: 'dynamic', index: at('sel') }] }], enables: [tick], expression: { code: ['in', 0] } },
  { id: 'copier', type: 'SET', inputs: [at('pick:q0'), at('copy')], enables: [at('branch:q0')] },
  { id: 'above', type: 'EXPR', inputs: [at('count')], enables: [tick], expression: { constants: [3], code: ['in', 0, 'const', 0, '>'] } },
  { id: 'branch', type: 'IF', inputs: [at('above:q0')], eno: true },
  { id: 'hold', type: 'LATCH', inputs: [at('branch:q0'), null], settings: { mode: 'SET_DOMINANT' } },
  { id: 'delay', type: 'TIMER', inputs: [at('hold:q0')], settings: { mode: 'TON', time_base: 'MS', pt: 300 } },
  { id: 'rise', type: 'EDGE', inputs: [at('count')], settings: { edge_type: 'RISING', change_by: 1 } },
  { id: 'seen', type: 'LATCH', inputs: [at('rise:q0'), null] },
  { id: 'route', type: 'SWITCH', inputs: [at('sel')], outputs: [null, null, null] },
  { id: 'loop', type: 'FOR', body: 1, settings: { k_start: 0, k_end: 3, k_step: 1, max_turns: 10, op: 'ADD', cmp: 'LT' } },
  { id: 'sum', type: 'EXPR', inputs: [at('acc'), at('loop:q0')], enables: [at('loop:eno')], outputs: ['acc'], expression: { constants: [1], code: ['in', 0, 'in', 1, '+', 'const', 0, '+'] } },
]

export const sampleProject = (): ProjectDocument => ({
  ...createProject('Sample program'),
  objects,
  canvases: recoveredCanvases(blocks).map((canvas) => ({ ...canvas, name: 'Sample' })),
})
