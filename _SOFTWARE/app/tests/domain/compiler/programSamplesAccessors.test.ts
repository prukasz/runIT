import { describe, expect, it } from 'vitest'
import { runitVmCatalog } from '../../../src/domain/descriptors'
import { createProject } from '../../../src/domain/project'
import type { ObjectNode, ObjectPath, PathStep, ProgramBlock, ValueNode } from '../../../src/domain/project'
import { compileProgram, packExec, packSubscribe, packValueWrite } from '../../../src/domain/compiler'

/*
 * Accessors under real blocks: dynamic positions that a block drives (idx = n
 * mod 3), a dynamic inside a dynamic (table[map[idx]]), a folder picked by a
 * live position (bank[sel].v), a write through a dynamic destination (SET into
 * dst[idx]), CLONE into a live cell read back by position and name, and a
 * position that leaves its array at run time (oob, written live).
 * Written to fixtures/program-samples-accessors.json;
 * `.claude/skills/runit-esp/scripts/accessor_program_test.py` runs it in four
 * phases (each a live write), logs everything and checks `checks`.
 */

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join('')
const value = (id: string, type: string, initial: ValueNode['value'], length = 1): ValueNode => ({ kind: 'value', id, name: id, type, length, value: initial, mutable: true, retentive: false })
const folder = (id: string, ...children: ObjectNode[]): ObjectNode => ({ kind: 'folder', id, name: id, children })
const index = (at: number): PathStep => ({ kind: 'index', index: at })
const name = (text: string): PathStep => ({ kind: 'name', name: text })
const dynamic = (path: ObjectPath): PathStep => ({ kind: 'dynamic', index: path })
const path = (root: string, ...steps: PathStep[]): ObjectPath => ({ root, steps })
const tick = path('every:eno')

const objects: readonly ObjectNode[] = [
  value('n', 'F', [0]),
  value('sel', 'U8', [1]),
  value('idx', 'U8', [0]),
  value('oob', 'U8', [2]),
  value('map', 'U8', [2, 0, 1], 3),
  value('table', 'F', [100, 200, 300, 400], 4),
  value('dst', 'F', [0, 0, 0], 3),
  folder('bank', folder('a', value('av', 'F', [11])), folder('b', value('bv', 'F', [21])), folder('c', value('cv', 'F', [31]))),
  folder('msg', value('mtemp', 'F', [1]), value('mhum', 'F', [40])),
  folder('box', folder('copy', value('ctemp', 'F', [0]))),
]

const blocks: readonly ProgramBlock[] = [
  { id: 'every', type: 'PERIODIC', settings: { period: 100, time_base: 'MS' } },
  { id: 'cnt', type: 'EXPR', inputs: [path('n')], outputs: ['n'], enables: [tick], expression: { constants: [1], code: ['in', 0, 'const', 0, '+'] } },
  { id: 'mkidx', type: 'EXPR', inputs: [path('n')], outputs: ['idx'], enables: [tick], expression: { constants: [3], code: ['in', 0, 'const', 0, 'mod'] } },
  { id: 'twice', type: 'EXPR', inputs: [path('table', dynamic(path('map', dynamic(path('idx')))))], enables: [tick], expression: { code: ['in', 0] } },
  { id: 'pickbank', type: 'EXPR', inputs: [path('bank', dynamic(path('sel')), index(0), index(0))], enables: [tick], expression: { code: ['in', 0] } },
  { id: 'stamp', type: 'SET', inputs: [path('n'), path('dst', dynamic(path('idx')))] },
  { id: 'feed', type: 'EXPR', inputs: [path('n')], outputs: ['mtemp'], enables: [tick], expression: { code: ['in', 0] } },
  { id: 'clone', type: 'CLONE', inputs: [path('msg'), path('box', index(0))], enables: [tick] },
  { id: 'cloned', type: 'EXPR', inputs: [path('box', index(0), name('mtemp'), index(0))], enables: [tick], expression: { code: ['in', 0] } },
  { id: 'clonedhum', type: 'EXPR', inputs: [path('box', index(0), name('mhum'), index(0))], enables: [tick], expression: { code: ['in', 0] } },
  { id: 'far', type: 'EXPR', inputs: [path('table', dynamic(path('oob')))], enables: [tick], expression: { code: ['in', 0] } },
]

/** Live writes between phases: phase p + 1 starts after writes[p]. */
const writes: readonly { id: string; values: readonly number[]; what: string }[] = [
  { id: 'sel', values: [2], what: 'bank[sel] moves to c' },
  { id: 'oob', values: [9], what: 'table[oob] leaves the array (length 4)' },
  { id: 'oob', values: [3], what: 'table[oob] is back inside' },
]

type Check = { id: string; elem?: number; op: 'eq' | 'gt' | 'within' | 'hits'; value: number; max?: number; phase: 0 | 1 | 2 | 3; what: string }

/** Over the samples of a phase: eq / gt on the last, `within` [value, max], `hits` = some sample equals value; `elem` picks an array element. */
const checks: readonly Check[] = [
  { id: 'idx', op: 'within', value: 0, max: 2, phase: 0, what: 'a block writes a U8 position (n mod 3)' },
  ...[0, 1, 2].map((v): Check => ({ id: 'idx', op: 'hits', value: v, phase: 0, what: `position reaches ${v}` })),
  { id: 'twice:q0', op: 'hits', value: 300, phase: 0, what: 'table[map[idx]]: map[0] = 2' },
  { id: 'twice:q0', op: 'hits', value: 100, phase: 0, what: 'table[map[idx]]: map[1] = 0' },
  { id: 'twice:q0', op: 'hits', value: 200, phase: 0, what: 'table[map[idx]]: map[2] = 1' },
  { id: 'pickbank:q0', op: 'eq', value: 21, phase: 0, what: 'bank[sel = 1] reads folder b' },
  { id: 'pickbank:q0', op: 'eq', value: 31, phase: 1, what: 'live sel = 2 moves the folder pick to c' },
  ...[0, 1, 2].map((elem): Check => ({ id: 'dst', elem, op: 'gt', value: 0, phase: 0, what: `SET reached dst[${elem}] through a dynamic destination` })),
  { id: 'cloned:q0', op: 'gt', value: 5, phase: 0, what: "CLONE's copy of msg tracks the counter (box[0].mtemp by name)" },
  { id: 'clonedhum:q0', op: 'eq', value: 40, phase: 0, what: 'the clone carries the other field too (box[0].mhum)' },
  { id: 'far:q0', op: 'eq', value: 300, phase: 0, what: 'table[oob = 2]' },
  { id: 'far:q0', op: 'eq', value: 400, phase: 3, what: 'table[oob = 3] after coming back' },
]

describe('program samples accessors', () => {
  it('matches the saved fixture', async () => {
    const catalog = runitVmCatalog()
    const maxFrameBytes = 240
    const compiled = compileProgram({ ...createProject('accessor program'), objects }, catalog, { maxFrameBytes, blocks, liveCells: new Set(['copy']) })
    expect(compiled.diagnostics).toEqual([])
    const wire = (id: string) => compiled.objects.wireIdOf.get(id)!
    const flat = (nodes: readonly ObjectNode[]): ObjectNode[] => nodes.flatMap((node) => (node.kind === 'folder' ? [node, ...flat(node.children)] : [node]))
    const node = (id: string) => flat(objects).find((candidate) => candidate.id === id) as ValueNode
    const all = [...compiled.objects.wireIdOf.entries()].filter(([id, w]) => compiled.objects.objects[w]!.node.kind === 'value' && compiled.objects.objects[w]!.node.id === id)
    const built = {
      frames: compiled.frames.map(hex),
      counts: compiled.counts,
      arenaBytes: compiled.arenaBytes,
      subscribe: hex(packSubscribe(catalog, all.map(([, id]) => id), maxFrameBytes)),
      run: hex(packExec(catalog, catalog.execCommands.get('NORMAL_MODE')!)),
      writes: writes.map((write) => ({ what: write.what, frames: packValueWrite(catalog, compiled.objects, node(write.id), [...write.values], maxFrameBytes).map(hex) })),
      blocks: compiled.blocks.blocks.map((block) => `${block.wireId} ${block.id} ${block.type}`),
      objects: all.map(([id, w]) => ({ id, wire: w, type: compiled.objects.objects[w]!.type.key, length: compiled.objects.objects[w]!.elements })),
      checks: checks.map((check) => ({ ...check, wire: wire(check.id) })),
    }
    await expect(JSON.stringify(built, null, 2) + '\n').toMatchFileSnapshot('./fixtures/program-samples-accessors.json')
  })
})
