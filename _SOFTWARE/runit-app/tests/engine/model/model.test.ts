import { describe, expect, it } from 'vitest'
import {
  addVariable,
  checkVariables,
  flattenVariables,
  formatPath,
  layoutVariables,
  locate,
  moveVariable,
  parsePath,
  removeVariable,
  renameVariable,
  resolvePath,
} from '@engine/model'
import type { ObjectPath, Variable } from '@engine/model'
import { toHex } from '@engine/wire'

const value = (id: string, name: string, type: 'bool' | 'u8' | 'u32' | 'i32' | 'f32' | 'str' = 'f32', extra: object = {}): Variable => ({ kind: 'value', id, name, type, count: 1, ...extra })

/** The project of the screenshots: Run, drive-speed, current-limit, angles (arm1..arm3), Emergency-stop. */
const tree = (): Variable[] => [
  value('run', 'Run', 'bool'),
  value('speed', 'drive-speed'),
  value('limit', 'current-limit'),
  { kind: 'folder', id: 'angles', name: 'angles', children: [value('arm1', 'arm1'), value('arm2', 'arm2'), value('arm3', 'arm3')] },
  value('estop', 'Emergency-stop', 'bool'),
]

describe('variables: editing returns a new tree', () => {
  it('adds at the top, into a folder, at a position', () => {
    const t = addVariable(tree(), 'angles', value('arm4', 'arm4'), 1)
    expect(flattenVariables(t).map((v) => v.id)).toEqual(['run', 'speed', 'limit', 'angles', 'arm1', 'arm4', 'arm2', 'arm3', 'estop'])
    expect(() => addVariable(tree(), 'run', value('x', 'x'))).toThrow(/not a folder/)
  })

  it('removes a variable with what is in it and unpoints the references to it', () => {
    const withRef = addVariable(tree(), null, { kind: 'reference', id: 'alias', name: 'alias', target: 'arm2' })
    const t = removeVariable(withRef, 'angles')
    expect(flattenVariables(t).map((v) => v.id)).toEqual(['run', 'speed', 'limit', 'estop', 'alias'])
    expect(locate(t, 'alias')?.variable).toMatchObject({ kind: 'reference', target: null })
  })

  it('renames and moves, and refuses to move a folder into itself', () => {
    expect(locate(renameVariable(tree(), 'speed', 'rpm'), 'speed')?.variable.name).toBe('rpm')
    const moved = moveVariable(tree(), 'arm1', null, 0)
    expect(flattenVariables(moved).slice(0, 2).map((v) => v.id)).toEqual(['arm1', 'run'])
    expect(() => moveVariable(tree(), 'angles', 'arm1')).toThrow(/not a folder|itself/)
    expect(() => moveVariable(addVariable(tree(), 'angles', { kind: 'folder', id: 'inner', name: 'inner', children: [] }), 'angles', 'inner')).toThrow(/itself/)
  })

  it('does not change the tree it was given', () => {
    const original = tree()
    const copy = JSON.stringify(original)
    addVariable(original, 'angles', value('x', 'x'))
    removeVariable(original, 'angles')
    renameVariable(original, 'run', 'go')
    expect(JSON.stringify(original)).toBe(copy)
  })
})

describe('variables: checks', () => {
  const codes = (t: Variable[]) => checkVariables(t).map((p) => p.code)

  it('accepts the project', () => expect(codes(tree())).toEqual([]))

  it('finds repeated names among siblings only', () => {
    expect(codes([...tree(), value('x', 'Run')])).toContain('duplicate-name')
    expect(codes(addVariable(tree(), 'angles', value('x', 'Run')))).toEqual([])
  })

  it('finds bad names: empty, over 15 bytes (not characters), control characters', () => {
    expect(codes([value('a', '')])).toContain('bad-name')
    expect(codes([value('a', 'x'.repeat(16))])).toContain('bad-name')
    expect(codes([value('a', 'żółć-żółć-żółć')])).toContain('bad-name') // 14 characters, more than 15 bytes
    expect(codes([value('a', 'a\u0007b')])).toContain('bad-name')
    expect(codes([value('a', 'drive-speed')])).toEqual([])
  })

  it('finds repeated ids, too many initial values, a retained constant', () => {
    expect(codes([value('a', 'a'), value('a', 'b')])).toContain('duplicate-id')
    expect(codes([value('a', 'a', 'u8', { count: 2, initial: [1, 2, 3] })])).toContain('initial-too-long')
    expect(codes([value('a', 'a', 'str', { count: 3, initial: 'abcd' })])).toContain('initial-too-long')
    expect(codes([value('a', 'a', 'f32', { retained: true, writable: false })])).toContain('retained-constant')
  })

  it('finds references that point nowhere, at themselves, or at the folder that holds them', () => {
    expect(codes([{ kind: 'reference', id: 'r', name: 'r', target: 'zzz' }])).toContain('dangling-reference')
    expect(codes([{ kind: 'reference', id: 'r', name: 'r', target: 'r' }])).toContain('reference-cycle')
    expect(codes([{ kind: 'folder', id: 'f', name: 'f', children: [{ kind: 'reference', id: 'r', name: 'r', target: 'f' }] }])).toContain('reference-cycle')
  })
})

describe('layout: variables as objects', () => {
  it('numbers them depth-first, a folder before its children', () => {
    const { ids } = layoutVariables(tree())
    expect([...ids]).toEqual([['run', 0], ['speed', 1], ['limit', 2], ['angles', 3], ['arm1', 4], ['arm2', 5], ['arm3', 6], ['estop', 7]])
  })

  it('gives each object its type, name and flags', () => {
    const { objects } = layoutVariables([value('a', 'a', 'u32', { count: 4, retained: true, protected: true })])
    expect(objects).toEqual([{ id: 0, type: 'u32', count: 4, name: 'a', flags: { mutable: true, updResetable: true, retentive: true, usrProtected: true } }])
    expect(layoutVariables([value('a', 'a', 'f32', { writable: false })]).objects[0]?.flags.mutable).toBe(false)
  })

  it('a folder is a pointer object over its children, a reference one over its target', () => {
    const t: Variable[] = [...tree(), { kind: 'reference', id: 'alias', name: 'alias', target: 'speed' }]
    const { objects, data } = layoutVariables(t)
    const folder = objects.find((o) => o.name === 'angles')!
    expect(folder).toMatchObject({ type: 'ptr', count: 3 })
    expect(toHex(data.find((d) => d.id === folder.id)!.data)).toBe('040005000600')
    const alias = objects.find((o) => o.name === 'alias')!
    expect(toHex(data.find((d) => d.id === alias.id)!.data)).toBe('0100')
  })

  it('encodes initial values by type and leaves the rest zero', () => {
    const { data } = layoutVariables([value('a', 'a', 'f32', { initial: 1.5 }), value('b', 'b', 'u8', { count: 4, initial: [1, 2] }), value('c', 'c', 'str', { count: 8, initial: 'hi' }), value('d', 'd', 'i32')])
    expect(data.map((d) => [d.id, toHex(d.data)])).toEqual([[0, '0000c03f'], [1, '0102'], [2, '6869']])
  })

  it('leaves out an empty folder and what points at it', () => {
    const t: Variable[] = [{ kind: 'folder', id: 'f', name: 'f', children: [] }, { kind: 'reference', id: 'r', name: 'r', target: 'f' }]
    const { objects, skipped } = layoutVariables(t)
    expect(objects.map((o) => o.name)).toEqual(['r'])
    expect(skipped.map((s) => s.id)).toEqual(['f', 'r'])
  })
})

describe('paths: text', () => {
  const t = tree()
  const parse = (text: string) => {
    const result = parsePath(t, text)
    if (!result.ok) throw new Error(result.message)
    return result.path
  }

  it('parses a name, a child, an element, a quoted name, and a position read from another path', () => {
    expect(parse('Run')).toEqual({ root: 'run', steps: [] })
    expect(parse('angles.arm2')).toEqual({ root: 'angles', steps: [{ kind: 'name', name: 'arm2' }] })
    expect(parse('angles[1]')).toEqual({ root: 'angles', steps: [{ kind: 'index', index: 1 }] })
    expect(parse('angles["arm 2"]')).toEqual({ root: 'angles', steps: [{ kind: 'name', name: 'arm 2' }] })
    expect(parse('angles[drive-speed]')).toEqual({ root: 'angles', steps: [{ kind: 'dynamic', path: { root: 'speed', steps: [] } }] })
  })

  it('formats with names and round-trips', () => {
    for (const text of ['Run', 'angles.arm2', 'angles[1]', 'angles["arm 2"]', 'angles[drive-speed]']) {
      expect(formatPath(t, parse(text))).toBe(text)
    }
  })

  it('follows a rename (the root is an id)', () => {
    const path = parse('drive-speed')
    expect(formatPath(renameVariable(t, 'speed', 'rpm'), path)).toBe('rpm')
  })

  it('says where it stopped', () => {
    expect(parsePath(t, 'nope')).toMatchObject({ ok: false, at: 0 })
    expect(parsePath(t, 'angles.')).toMatchObject({ ok: false })
    expect(parsePath(t, 'angles[1')).toMatchObject({ ok: false })
    expect(parsePath(t, 'Run x')).toMatchObject({ ok: false })
  })
})

describe('paths: what they name', () => {
  const t: Variable[] = [
    ...tree(),
    value('table', 'table', 'u8', { count: 4 }),
    { kind: 'folder', id: 'motors', name: 'motors', children: [value('left', 'left', 'f32', { count: 3 }), value('right', 'right', 'f32', { count: 3 })] },
    { kind: 'folder', id: 'mixed', name: 'mixed', children: [value('m1', 'm1', 'f32'), value('m2', 'm2', 'u8')] },
    { kind: 'reference', id: 'alias', name: 'alias', target: 'speed' },
  ]
  const at = (root: string, ...steps: ObjectPath['steps'][number][]): ObjectPath => ({ root, steps })
  const ok = (path: ObjectPath) => {
    const r = resolvePath(t, path)
    if (!r.ok) throw new Error(`${r.code}: ${r.message}`)
    return r.target
  }
  const code = (path: ObjectPath) => {
    const r = resolvePath(t, path)
    return r.ok ? 'ok' : r.code
  }

  it('a path with no steps is the object itself, a folder included', () => {
    expect(ok(at('speed'))).toMatchObject({ type: 'f32', count: 1, element: null })
    expect(ok(at('angles'))).toMatchObject({ type: 'ptr', count: 3, element: null })
  })

  it('a step into a folder names the child object, by position or by name', () => {
    expect(ok(at('angles', { kind: 'index', index: 1 })).variable.id).toBe('arm2')
    expect(ok(at('angles', { kind: 'name', name: 'arm3' })).variable.id).toBe('arm3')
  })

  it('a step into an array names an element, and the path ends there', () => {
    expect(ok(at('table', { kind: 'index', index: 3 }))).toMatchObject({ type: 'u8', count: 1, element: 3 })
    expect(ok(at('motors', { kind: 'name', name: 'left' }, { kind: 'index', index: 2 }))).toMatchObject({ element: 2, type: 'f32' })
  })

  it('a reference is entered like a folder of one child', () => {
    expect(ok(at('alias', { kind: 'index', index: 0 })).variable.id).toBe('speed')
    expect(code(at('alias', { kind: 'index', index: 1 }))).toBe('ERR_VM_ACCESSOR_OOB')
  })

  it('reports where and why a path fails, with the firmware error tag', () => {
    expect(code(at('nope'))).toBe('ERR_VM_ACCESSOR_UNKNOWN_ID')
    expect(code(at('table', { kind: 'index', index: 4 }))).toBe('ERR_VM_ACCESSOR_OOB')
    expect(code(at('angles', { kind: 'index', index: 3 }))).toBe('ERR_VM_ACCESSOR_OOB')
    expect(code(at('angles', { kind: 'name', name: 'arm9' }))).toBe('ERR_VM_ACCESSOR_NAME_NOT_FOUND')
    expect(code(at('table', { kind: 'name', name: 'x' }))).toBe('ERR_VM_OBJ_NOT_PTR')
    expect(code(at('speed', { kind: 'index', index: 0 }, { kind: 'index', index: 0 }))).toBe('ERR_VM_OBJ_NOT_PTR')
    const r = resolvePath(t, at('table', { kind: 'index', index: 9 }))
    expect(r).toMatchObject({ ok: false, at: 0 })
  })

  it('a position read from another path is run-time: the candidates decide the type', () => {
    const dyn = (inner: string): ObjectPath['steps'][number] => ({ kind: 'dynamic', path: at(inner) })
    expect(ok(at('table', dyn('speed')))).toMatchObject({ type: 'u8', element: null, dynamic: true })
    expect(ok(at('angles', dyn('speed')))).toMatchObject({ type: 'f32', dynamic: true }) // every child is a number
    expect(ok(at('mixed', dyn('speed')))).toMatchObject({ type: null, dynamic: true }) // the children differ
    expect(code(at('table', dyn('angles')))).toBe('ERR_VM_ACCESSOR_TYPE_MISMATCH') // the position must be a number
    expect(code(at('table', dyn('nope')))).toBe('ERR_VM_ACCESSOR_UNKNOWN_ID')
  })

  it('a step after a run-time position must fit every candidate', () => {
    const dyn: ObjectPath['steps'][number] = { kind: 'dynamic', path: at('speed') }
    expect(ok(at('motors', dyn, { kind: 'index', index: 2 }))).toMatchObject({ element: 2, dynamic: true })
    expect(code(at('motors', dyn, { kind: 'index', index: 3 }))).toBe('ERR_VM_ACCESSOR_OOB')
  })
})
