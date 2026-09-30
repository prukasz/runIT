import { describe, expect, it } from 'vitest'
import { runitVmCatalog } from '../../../src/domain/descriptors'
import { createProject } from '../../../src/domain/project'
import type { ObjectNode, ObjectPath, ProgramBlock, ValueNode } from '../../../src/domain/project'
import { compileProgram, packExec, packSubscribe, packValueWrite } from '../../../src/domain/compiler'

/*
 * A whole program compiled by the app (objects, accessors, blocks) for the
 * board, written to fixtures/program-samples.json with the subscribe, run and
 * live-write frames. `.claude/skills/runit-esp/scripts/program_samples_test.py`
 * sends it and checks the values the board reports: `checks[].phase` 0 while
 * running, 1 after `write` (sel = 0).
 */

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join('')
const value = (id: string, type: string, initial?: ValueNode['value'], length = 1): ValueNode => ({ kind: 'value', id, name: id, type, length, ...(initial ? { value: initial } : {}), mutable: true, retentive: false })
const at = (root: string): ObjectPath => ({ root })
const tick = at('every:eno')

const objects: readonly ObjectNode[] = [
  value('count', 'F'),
  value('sel', 'U8', [2]),
  value('table', 'U32', [100, 200, 300, 400], 4),
  value('copy', 'F'),
  value('acc', 'F'),
]

const blocks: readonly ProgramBlock[] = [
  { id: 'every', type: 'PERIODIC', settings: { period: 100, time_base: 'MS' } },
  { id: 'counter', type: 'EXPR', inputs: [at('count')], outputs: ['count'], enables: [tick], expression: { constants: [1], code: ['in', 0, 'const', 0, '+'] } },
  { id: 'pick', type: 'EXPR', inputs: [{ root: 'table', steps: [{ kind: 'dynamic', index: at('sel') }] }], enables: [tick], expression: { code: ['in', 0] } },
  { id: 'copier', type: 'SET', inputs: [at('pick:q0'), at('copy')] },
  { id: 'above', type: 'EXPR', inputs: [at('count')], enables: [tick], expression: { constants: [3], code: ['in', 0, 'const', 0, '>'] } },
  { id: 'branch', type: 'IF', inputs: [at('above:q0')], eno: true },
  { id: 'hold', type: 'LATCH', inputs: [at('branch:q0'), null], settings: { mode: 'SET_DOMINANT' } },
  { id: 'delay', type: 'TIMER', inputs: [at('hold:q0')], eno: true, settings: { mode: 'TON', time_base: 'MS', pt: 300 } },
  { id: 'rise', type: 'EDGE', inputs: [at('count')], settings: { edge_type: 'RISING', change_by: 1 } },
  { id: 'seen', type: 'LATCH', inputs: [at('rise:q0'), null] },
  { id: 'route', type: 'SWITCH', inputs: [at('sel')], outputs: [null, null, null] },
  { id: 'loop', type: 'FOR', body: 1, settings: { k_start: 0, k_end: 3, k_step: 1, max_turns: 10, op: 'ADD', cmp: 'LT' } },
  { id: 'sum', type: 'EXPR', inputs: [at('acc'), at('loop:q0')], outputs: ['acc'], expression: { constants: [1], code: ['in', 0, 'in', 1, '+', 'const', 0, '+'] } },
]

/** What the board should report; `op` eq / gt / mod (a positive multiple of value). */
const checks: readonly { id: string; op: 'eq' | 'gt' | 'mod'; value: number; phase: 0 | 1; what: string }[] = [
  { id: 'count', op: 'gt', value: 5, phase: 0, what: 'PERIODIC → EXPR counts ticks into a user object' },
  { id: 'pick:q0', op: 'eq', value: 300, phase: 0, what: 'EXPR reads table[sel] (dynamic accessor)' },
  { id: 'copy', op: 'eq', value: 300, phase: 0, what: 'SET copies a block output into a user object' },
  { id: 'above:q0', op: 'eq', value: 1, phase: 0, what: 'EXPR with a constant: count > 3' },
  { id: 'branch:q0', op: 'eq', value: 1, phase: 0, what: 'IF yes' },
  { id: 'branch:eno', op: 'eq', value: 1, phase: 0, what: 'IF ENO object' },
  { id: 'hold:q0', op: 'eq', value: 1, phase: 0, what: 'LATCH set by IF' },
  { id: 'delay:eno', op: 'eq', value: 1, phase: 0, what: 'TIMER on-delay 300 ms (its Q is its ENO)' },
  { id: 'seen:q0', op: 'eq', value: 1, phase: 0, what: 'EDGE (a rise of at least 1 within one pass) latched' },
  { id: 'route:q2', op: 'eq', value: 1, phase: 0, what: 'SWITCH drives branch sel = 2' },
  { id: 'acc', op: 'mod', value: 6, phase: 0, what: 'FOR 0..2 runs its body 3 times a pass (+6)' },
  { id: 'pick:q0', op: 'eq', value: 100, phase: 1, what: 'the dynamic accessor follows sel = 0' },
  { id: 'copy', op: 'eq', value: 100, phase: 1, what: 'SET follows' },
  { id: 'route:q0', op: 'eq', value: 1, phase: 1, what: 'SWITCH drives branch 0' },
  { id: 'route:q2', op: 'eq', value: 0, phase: 1, what: 'SWITCH clears branch 2' },
]

describe('program samples', () => {
  it('matches the saved fixture', async () => {
    const catalog = runitVmCatalog()
    const project = { ...createProject('program samples'), objects }
    const maxFrameBytes = 240
    const compiled = compileProgram(project, catalog, { maxFrameBytes, blocks })
    expect(compiled.diagnostics).toEqual([])
    const wire = (id: string) => compiled.objects.wireIdOf.get(id)!
    const typeOf = (id: string) => compiled.objects.objects[wire(id)]!.type.key
    const watched = [...new Set(checks.map((check) => check.id))]
    const built = {
      frames: compiled.frames.map(hex),
      counts: compiled.counts,
      arenaBytes: compiled.arenaBytes,
      subscribe: hex(packSubscribe(catalog, watched.map(wire), maxFrameBytes)),
      run: hex(packExec(catalog, catalog.execCommands.get('NORMAL_MODE')!)),
      write: packValueWrite(catalog, compiled.objects, objects[1] as ValueNode, [0], maxFrameBytes).map(hex),
      blocks: compiled.blocks.blocks.map((block) => `${block.wireId} ${block.id} ${block.type}`),
      checks: checks.map((check) => ({ ...check, wire: wire(check.id), type: typeOf(check.id) })),
    }
    await expect(JSON.stringify(built, null, 2) + '\n').toMatchFileSnapshot('./fixtures/program-samples.json')
  })
})
