import { describe, expect, it } from 'vitest'
import { runitVmCatalog } from '../descriptors'
import { createProject } from '../project'
import type { ObjectNode, ObjectPath, PathStep, ProgramBlock, ValueNode } from '../project'
import { compileProgram, packExec, packSubscribe, packValueWrite } from '.'

/*
 * Second whole-program sample: a free-running sawtooth (n mod 8) driving the
 * bit expression, both timers, a falling edge into an RS latch and a folder
 * read by name and by a dynamic index. Every value object is subscribed.
 * Written to fixtures/program-samples-b.json;
 * `.claude/skills/runit-esp/scripts/program_samples_b_test.py` sends it, logs
 * everything the board reports and checks `checks` (values change over time,
 * so ops look at all samples): phase 0 while running, 1 after `write` (k = 3).
 */

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join('')
const value = (id: string, type: string, initial?: ValueNode['value'], length = 1): ValueNode => ({ kind: 'value', id, name: id, type, length, ...(initial ? { value: initial } : {}), mutable: true, retentive: false })
const at = (root: string, ...steps: PathStep[]): ObjectPath => ({ root, steps })
const tick = at('every:eno')

const objects: readonly ObjectNode[] = [
  value('n', 'F'),
  value('bits', 'U32'),
  value('scaled', 'F'),
  value('picked', 'F'),
  value('k', 'U8', [1]),
  { kind: 'folder', id: 'cfg', name: 'cfg', children: [value('gains', 'F', [1.5, 2.5, 3.5, 4.5], 4)] },
]

const blocks: readonly ProgramBlock[] = [
  { id: 'every', type: 'PERIODIC', settings: { period: 50, time_base: 'MS' } },
  { id: 'cnt', type: 'EXPR', inputs: [at('n')], outputs: ['n'], enables: [tick], expression: { constants: [1], code: ['in', 0, 'const', 0, '+'] } },
  { id: 'saw', type: 'EXPR', inputs: [at('n')], enables: [tick], expression: { constants: [8], code: ['in', 0, 'const', 0, 'mod'] } },
  { id: 'sq', type: 'EXPR', inputs: [at('saw:q0')], enables: [tick], expression: { code: ['in', 0, 'dup', '*'] } },
  { id: 'shift', type: 'EXPR_BIT', inputs: [at('saw:q0')], outputs: ['bits'], enables: [tick], expression: { constants: [1], code: ['in', 0, 'const', 0, '<<', 'const', 0, '|'] } },
  { id: 'gain', type: 'EXPR', inputs: [at('saw:q0'), at('cfg', { kind: 'name', name: 'gains' }, { kind: 'index', index: 2 })], outputs: ['scaled'], enables: [tick], expression: { code: ['in', 0, 'in', 1, '*'] } },
  { id: 'dyn', type: 'EXPR', inputs: [at('cfg', { kind: 'name', name: 'gains' }, { kind: 'dynamic', index: at('k') })], outputs: ['picked'], enables: [tick], expression: { code: ['in', 0] } },
  { id: 'high', type: 'EXPR', inputs: [at('saw:q0')], enables: [tick], expression: { constants: [3], code: ['in', 0, 'const', 0, '>'] } },
  { id: 'hi', type: 'IF', inputs: [at('high:q0')], eno: true },
  { id: 'off', type: 'TIMER', inputs: [at('hi:q0')], settings: { mode: 'TOF', time_base: 'MS', pt: 200 } },
  { id: 'pulse', type: 'TIMER', inputs: [at('hi:q0')], settings: { mode: 'TP', time_base: 'MS', pt: 150 } },
  { id: 'fall', type: 'EDGE', inputs: [at('saw:q0')], settings: { edge_type: 'FALLING', change_by: 3 } },
  { id: 'wrapped', type: 'LATCH', inputs: [at('fall:q0'), null], settings: { mode: 'RESET_DOMINANT' } },
]

/** Over every sample the board reports for `id`: eq / gt on the last, within [value, max], hits = some sample equals value. */
const checks: readonly { id: string; op: 'eq' | 'gt' | 'within' | 'hits'; value: number; max?: number; phase: 0 | 1; what: string }[] = [
  { id: 'n', op: 'gt', value: 10, phase: 0, what: 'PERIODIC 50 ms → EXPR counts ticks' },
  { id: 'saw:q0', op: 'within', value: 0, max: 7, phase: 0, what: 'n mod 8 stays in 0..7' },
  { id: 'saw:q0', op: 'hits', value: 7, phase: 0, what: 'saw reaches its top' },
  { id: 'sq:q0', op: 'hits', value: 49, phase: 0, what: 'dup * squares the top (7 → 49)' },
  { id: 'bits', op: 'hits', value: 15, phase: 0, what: 'EXPR_BIT (7 << 1) | 1 = 15' },
  { id: 'scaled', op: 'hits', value: 24.5, phase: 0, what: 'saw 7 × cfg.gains[2] (3.5) via a name path' },
  { id: 'picked', op: 'eq', value: 2.5, phase: 0, what: 'cfg.gains[k] with k = 1 (dynamic accessor)' },
  { id: 'hi:q0', op: 'hits', value: 1, phase: 0, what: 'IF yes while saw > 3' },
  { id: 'hi:q1', op: 'hits', value: 1, phase: 0, what: 'IF no while saw <= 3' },
  { id: 'off:q0', op: 'hits', value: 1, phase: 0, what: 'TOF output on' },
  { id: 'pulse:q0', op: 'hits', value: 1, phase: 0, what: 'TP fires' },
  { id: 'fall:q0', op: 'hits', value: 1, phase: 0, what: 'EDGE FALLING sees the wrap (7 → 0)' },
  { id: 'wrapped:q0', op: 'eq', value: 1, phase: 0, what: 'RS latch holds the wrap' },
  { id: 'picked', op: 'eq', value: 4.5, phase: 1, what: 'the dynamic accessor follows k = 3' },
]

describe('program samples B', () => {
  it('matches the saved fixture', async () => {
    const catalog = runitVmCatalog()
    const project = { ...createProject('program samples B'), objects }
    const maxFrameBytes = 240
    const compiled = compileProgram(project, catalog, { maxFrameBytes, blocks })
    expect(compiled.diagnostics).toEqual([])
    const wire = (id: string) => compiled.objects.wireIdOf.get(id)!
    const typeOf = (id: string) => compiled.objects.objects[wire(id)]!.type.key
    /** Every value object: folders carry no value. */
    const all = [...compiled.objects.wireIdOf.entries()].filter(([id, w]) => compiled.objects.objects[w]!.node.kind === 'value' && compiled.objects.objects[w]!.node.id === id)
    const built = {
      frames: compiled.frames.map(hex),
      counts: compiled.counts,
      arenaBytes: compiled.arenaBytes,
      subscribe: hex(packSubscribe(catalog, all.map(([, w]) => w), maxFrameBytes)),
      run: hex(packExec(catalog, catalog.execCommands.get('NORMAL_MODE')!)),
      write: packValueWrite(catalog, compiled.objects, objects[4] as ValueNode, [3], maxFrameBytes).map(hex),
      blocks: compiled.blocks.blocks.map((block) => `${block.wireId} ${block.id} ${block.type}`),
      objects: all.map(([id, w]) => ({ id, wire: w, type: typeOf(id) })),
      checks: checks.map((check) => ({ ...check, wire: wire(check.id), type: typeOf(check.id) })),
    }
    await expect(JSON.stringify(built, null, 2) + '\n').toMatchFileSnapshot('./fixtures/program-samples-b.json')
  })
})
