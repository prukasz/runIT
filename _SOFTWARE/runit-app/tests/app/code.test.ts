import { describe, expect, it } from 'vitest'
import { checkVariables, createEngine, moveVariable } from '@engine'
import type { ValueVariable, Variable } from '@engine'
import { blk, num, project, read } from '../engine/helpers'
import { blockUses, dropPlace, folderChoices, isVariableTarget, initialText, maxCount, newVariable, parseInitial, pathOf, retype, targetChoices } from '../../src/app/code/variables'

const tree: Variable[] = [
  { kind: 'value', id: 'speed', name: 'speed', type: 'f32', count: 1 },
  {
    kind: 'folder', id: 'arm', name: 'arm', children: [
      { kind: 'value', id: 'a1', name: 'a1', type: 'i32', count: 3 },
      { kind: 'folder', id: 'grip', name: 'grip', children: [{ kind: 'value', id: 'g', name: 'g', type: 'bool', count: 1 }] },
    ],
  },
  { kind: 'reference', id: 'r', name: 'r', target: null },
]
const value = (patch: Partial<ValueVariable>): ValueVariable => ({ kind: 'value', id: 'x', name: 'x', type: 'f32', count: 1, ...patch })

describe('Code view: variables', () => {
  it('a new variable has a free name and id, and the tree stays valid', () => {
    const added = newVariable('value', tree, tree)
    expect(added).toMatchObject({ kind: 'value', name: 'var1', type: 'f32', count: 1 })
    expect(newVariable('folder', tree, tree).name).toBe('folder1')
    const next = [...tree, added, { ...newVariable('reference', [...tree, added], tree), target: 'speed' } as Variable]
    expect(checkVariables(next)).toEqual([])
  })

  it('reads the initial value as the type takes it, and gives it back as text', () => {
    expect(parseInitial(value({}), '2.5')).toEqual({ ok: true, initial: 2.5 })
    expect(parseInitial(value({ count: 3 }), '1, 2 3')).toEqual({ ok: true, initial: [1, 2, 3] })
    expect(parseInitial(value({ count: 2 }), '1, 2, 3')).toMatchObject({ ok: false })
    expect(parseInitial(value({ type: 'u8' }), '256')).toMatchObject({ ok: false })
    expect(parseInitial(value({ type: 'i32' }), '1.5')).toMatchObject({ ok: false })
    expect(parseInitial(value({ type: 'bool', count: 2 }), 'true off')).toEqual({ ok: true, initial: [true, false] })
    expect(parseInitial(value({ type: 'str', count: 8 }), 'a, b')).toEqual({ ok: true, initial: 'a, b' })
    expect(parseInitial(value({}), '')).toEqual({ ok: true, initial: undefined })
    expect(initialText(value({ count: 3, initial: [1, 2, 3] }))).toBe('1, 2, 3')
  })

  it('a new type drops the old initial value and sizes text in bytes', () => {
    expect(retype(value({ initial: 3 }), 'str')).toEqual({ type: 'str', initial: undefined, count: 32 })
    expect(retype(value({ type: 'str', count: 40 }), 'u8')).toMatchObject({ count: 1 })
    expect(maxCount('u8')).toBe(0xffff)
    expect(maxCount('f32')).toBe(Math.floor(0xffff / 4))
  })

  it('a value at its limit compiles; one more element is refused', () => {
    const engine = createEngine()
    const at = (count: number) => engine.compile({ variables: [value({ type: 'f32', count })], blocks: [] })
    expect(at(maxCount('f32')).diagnostics.filter((d) => d.severity === 'error')).toEqual([])
    expect(at(maxCount('f32') + 1).ok).toBe(false)
  })

  it('names a variable by its path and offers only the folders and targets that make no cycle', () => {
    expect(pathOf(tree, 'g')).toBe('arm.grip.g')
    expect(folderChoices(tree, 'arm').map((c) => c.value)).toEqual([])
    expect(folderChoices(tree, 'speed').map((c) => c.label)).toEqual(['arm', 'arm.grip'])
    const nested: Variable[] = [{ kind: 'folder', id: 'f', name: 'f', children: [{ kind: 'reference', id: 'in', name: 'in', target: null }] }, ...tree]
    expect(targetChoices(nested, 'in').map((c) => c.value)).not.toContain('f')
    expect(targetChoices(nested, 'in').map((c) => c.value)).not.toContain('in')
  })

  it('lists the block pins that read or write a variable, an element of it, or a position from it', () => {
    const p = project([num('x', 'speed'), num('y', 'limit'), num('z', 'out'), { kind: 'value', id: 't', name: 'table', type: 'f32', count: 4 }], [
      blk('e', 'expr', { groups: { inputs: 2 }, inputs: [read('x'), { kind: 'variable', path: { root: 't', steps: [{ kind: 'dynamic', path: { root: 'y', steps: [] } }] } }], outputs: [{ path: { root: 'z', steps: [] } }], enables: [read('x')], editor: { text: 'in0 + in1' } }),
    ])
    expect(blockUses(p, 'x')).toEqual([{ block: 'e', role: 'input', index: 0 }, { block: 'e', role: 'enable', index: 0 }])
    expect(blockUses(p, 'y')).toEqual([{ block: 'e', role: 'input', index: 1 }])
    expect(blockUses(p, 't')).toEqual([{ block: 'e', role: 'input', index: 1 }])
    expect(blockUses(p, 'z')).toEqual([{ block: 'e', role: 'output', index: 0 }])
    expect(isVariableTarget({ kind: 'variable', variable: 'x' }, 'x')).toBe(true)
    expect(isVariableTarget({ kind: 'block', block: 'x' }, 'x')).toBe(false)
    expect(isVariableTarget(undefined, 'x')).toBe(false)
  })

  it('a drop lands where the row shows it, and never puts a folder into itself', () => {
    expect(dropPlace(tree, 'speed', 'arm', 'into')).toEqual({ parent: 'arm' })
    expect(dropPlace(tree, 'arm', 'grip', 'into')).toBeNull()
    // Down in the same list: after `arm` (index 1) is index 1 once `speed` (index 0) is taken out.
    const down = dropPlace(tree, 'speed', 'arm', 'after')!
    expect(moveVariable(tree, 'speed', down.parent, down.index).map((v) => v.id)).toEqual(['arm', 'speed', 'r'])
    const up = dropPlace(tree, 'r', 'speed', 'before')!
    expect(moveVariable(tree, 'r', up.parent, up.index).map((v) => v.id)).toEqual(['r', 'speed', 'arm'])
    const out = dropPlace(tree, 'g', 'speed', 'after')!
    expect(out).toEqual({ parent: null, index: 1 })
  })
})
