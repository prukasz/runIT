import { describe, expect, it } from 'vitest'
import { runitVmCatalog } from '../../../src/domain/descriptors'
import { createProject } from '../../../src/domain/project'
import type { ObjectNode, ObjectPath, ObjectSection, PathStep, ValueNode } from '../../../src/domain/project'
import { compileProgram } from '../../../src/domain/compiler'

/*
 * Accessor samples for the board: a program of objects and nested / dynamic
 * accessors as the compiler builds it, written to fixtures/accessor-samples.json.
 * `.claude/skills/runit-esp/scripts/accessor_samples_test.py` uploads it, adds
 * a PERIODIC block (output `tick`) and one EXPR block per path (output = the
 * path's value, triggered by `tick`) and checks the values the board reports,
 * before and after a live write to `sel`.
 */

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join('')
const value = (id: string, type: string, initial: ValueNode['value'], length = 1): ValueNode => ({ kind: 'value', id, name: id, type, length, value: initial, mutable: true, retentive: false })
const index = (at: number): PathStep => ({ kind: 'index', index: at })
const name = (text: string): PathStep => ({ kind: 'name', name: text })
const dynamic = (path: ObjectPath): PathStep => ({ kind: 'dynamic', index: path })
const path = (root: string, ...steps: PathStep[]): ObjectPath => ({ root, steps })

const objects: readonly ObjectNode[] = [
  { kind: 'folder', id: 'motor', name: 'motor', children: [value('speed', 'F', [1.5]), value('gains', 'F', [10, 20, 30, 40], 4)] },
  value('sel', 'U8', [2]),
  value('pick', 'U8', [3, 0, 1, 2], 4),
  value('table', 'U32', [100, 200, 300, 400], 4),
  { kind: 'folder', id: 'ch', name: 'ch', children: [value('a', 'F', [1]), value('b', 'F', [2]), value('c', 'F', [3])] },
  { kind: 'folder', id: 'box', name: 'box', children: [{ kind: 'folder', id: 'copy', name: 'copy', children: [value('temp', 'F', [21.5])] }] },
]

/** Each path, and its value with sel = 2, then with sel = 0. */
const samples: readonly { key: string; path: ObjectPath; expect: readonly [number, number] }[] = [
  { key: 'nested', path: path('motor', index(1), index(2)), expect: [30, 30] },
  { key: 'by-name', path: path('motor', name('gains'), index(3)), expect: [40, 40] },
  { key: 'dynamic', path: path('table', dynamic(path('sel'))), expect: [300, 100] },
  { key: 'dynamic-nested', path: path('gains', dynamic(path('pick', dynamic(path('sel'))))), expect: [20, 40] },
  { key: 'dynamic-child', path: path('ch', dynamic(path('sel')), index(0)), expect: [3, 1] },
  { key: 'live-names', path: path('box', name('copy'), name('temp'), index(0)), expect: [21.5, 21.5] },
]

const outputs: ObjectSection = {
  key: 'blocks',
  owner: 'block',
  objects: [
    ...samples.map((sample): ValueNode => ({ kind: 'value', id: `out:${sample.key}`, name: '', type: 'F', length: 1, mutable: true, retentive: false })),
    { kind: 'value', id: 'tick', name: '', type: 'B', length: 1, mutable: true, retentive: false },
  ],
}

describe('accessor samples', () => {
  it('matches the saved fixture', async () => {
    const catalog = runitVmCatalog()
    const compiled = compileProgram({ ...createProject('accessor samples'), objects }, catalog, {
      maxFrameBytes: 64,
      sections: [outputs],
      accessors: [...samples.map(({ key, path: target }) => ({ key, path: target })), { key: 'tick', path: { root: 'tick' } }],
      liveCells: new Set(['copy']),
    })
    expect(compiled.diagnostics).toEqual([])
    const built = {
      frames: compiled.frames.map(hex),
      counts: compiled.counts,
      arenaBytes: compiled.arenaBytes,
      sel: compiled.objects.wireIdOf.get('sel'),
      tick: { output: compiled.objects.wireIdOf.get('tick'), accessor: compiled.accessors.wireIdOf.get('tick') },
      samples: samples.map((sample) => ({
        key: sample.key,
        accessor: compiled.accessors.wireIdOf.get(sample.key),
        steps: compiled.accessors.accessors[compiled.accessors.wireIdOf.get(sample.key)!]!.steps,
        output: compiled.objects.wireIdOf.get(`out:${sample.key}`),
        expect: sample.expect,
      })),
    }
    await expect(JSON.stringify(built, null, 2) + '\n').toMatchFileSnapshot('./fixtures/accessor-samples.json')
  })
})
