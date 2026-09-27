import { describe, expect, it } from 'vitest'
import { runitVmCatalog } from '../descriptors'
import { createProject } from '../project'
import type { ObjectNode, ObjectPath, PathStep, ProjectDocument, ValueNode } from '../project'
import { compileAccessors, compileObjects, compileProgram } from '.'
import type { AccessorRequest } from '.'

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join(' ')
const catalog = runitVmCatalog()
const value = (id: string, name: string, type: string, length = 1): ValueNode => ({ kind: 'value', id, name, type, length, mutable: true, retentive: false })
const project = (objects: readonly ObjectNode[]): ProjectDocument => ({ ...createProject('test'), objects })

// Wire IDs: motor 0, speed 1, gains 2, en 3, sel 4, table 5, box 6, copy 7, temp 8
const sample = project([
  { kind: 'folder', id: 'motor', name: 'motor', children: [value('speed', 'speed', 'F'), value('gains', 'gains', 'F', 4), value('en', 'en', 'B')] },
  value('sel', 'sel', 'U8'),
  value('table', 'table', 'U32', 8),
  { kind: 'folder', id: 'box', name: 'box', children: [{ kind: 'folder', id: 'copy', name: 'copy', children: [value('temp', 'temp', 'F')] }] },
])
const layout = compileObjects(sample, catalog).layout

const index = (at: number): PathStep => ({ kind: 'index', index: at })
const name = (text: string): PathStep => ({ kind: 'name', name: text })
const dynamic = (path: ObjectPath): PathStep => ({ kind: 'dynamic', index: path })
const path = (root: string, ...steps: PathStep[]): ObjectPath => ({ root, steps })
const compile = (requests: readonly AccessorRequest[], liveCells?: ReadonlySet<string>) => compileAccessors(requests, layout, catalog, { liveCells })
const one = (target: ObjectPath, liveCells?: ReadonlySet<string>) => {
  const compiled = compile([{ key: 'pin', path: target }], liveCells)
  const accessor = compiled.layout.accessors[compiled.layout.wireIdOf.get('pin') ?? -1]
  return { compiled, accessor }
}

describe('compileAccessors: nested paths', () => {
  it('names a whole object with no steps (cached by the device)', () => {
    const { compiled, accessor } = one(path('speed'))
    expect(compiled.diagnostics).toEqual([])
    expect(accessor).toMatchObject({ root: 1, steps: [], cached: true })
  })

  it('re-roots a path through fixed folder entries on the child', () => {
    const { accessor } = one(path('motor', index(1), index(2)))
    expect(accessor).toMatchObject({ root: 2, steps: [{ kind: 'literal', value: 2 }], cached: true })
  })

  it('keeps the folder step when the path ends on an entry: that is the slot', () => {
    const { accessor } = one(path('motor', index(1)))
    expect(accessor).toMatchObject({ root: 0, steps: [{ kind: 'literal', value: 1 }] })
  })

  it('turns a name into a position where the folder is known', () => {
    const { accessor } = one(path('motor', name('gains'), index(3)))
    expect(accessor).toMatchObject({ root: 2, steps: [{ kind: 'literal', value: 3 }] })
  })

  it('keeps names and skips checks below a live cell', () => {
    const { compiled, accessor } = one(path('box', name('copy'), name('temp'), index(0)), new Set(['copy']))
    expect(compiled.diagnostics).toEqual([])
    expect(accessor).toMatchObject({ root: 6, steps: [{ kind: 'name', name: 'copy' }, { kind: 'name', name: 'temp' }, { kind: 'literal', value: 0 }], cached: false })
  })
})

describe('compileAccessors: dynamic positions', () => {
  it('compiles the position to its own accessor with the lower ID', () => {
    const { compiled, accessor } = one(path('table', dynamic(path('sel'))))
    expect(compiled.diagnostics).toEqual([])
    expect(compiled.layout.accessors.map((entry) => [entry.root, entry.steps])).toEqual([
      [4, []],
      [5, [{ kind: 'ref', accessor: 0 }]],
    ])
    expect(accessor?.cached).toBe(false)
    // u16 acc_id, u16 root, u8 idx_count, u8 idx_len, then kind 1 (REF) + u16 accessor
    expect(compiled.records.map(hex)).toEqual(['00 00 04 00 00 00', '01 00 05 00 01 03 01 00 00'])
  })

  it('nests: a position read through another dynamic position', () => {
    const { compiled, accessor } = one(path('motor', index(1), dynamic(path('table', dynamic(path('sel'))))))
    expect(compiled.diagnostics).toEqual([])
    expect(accessor).toMatchObject({ root: 2, steps: [{ kind: 'ref', accessor: 1 }] })
  })

  it('checks nothing below a dynamic folder entry and keeps its names', () => {
    const { compiled, accessor } = one(path('box', dynamic(path('sel')), name('temp'), index(0)))
    expect(compiled.diagnostics).toEqual([])
    expect(accessor?.steps).toEqual([{ kind: 'ref', accessor: 0 }, { kind: 'name', name: 'temp' }, { kind: 'literal', value: 0 }])
  })

  it('warns when the position is read from an array', () => {
    const { compiled } = one(path('gains', dynamic(path('table'))))
    expect(compiled.diagnostics).toEqual([expect.objectContaining({ severity: 'warning', message: expect.stringContaining('only the first') })])
  })

  it('refuses a position read from a folder', () => {
    const { compiled } = one(path('table', dynamic(path('motor'))))
    expect(compiled.diagnostics).toEqual([expect.objectContaining({ severity: 'error', firmwareError: 'ERR_VM_ACCESSOR_TYPE_MISMATCH' })])
  })

  it('refuses nesting deeper than the device resolves', () => {
    let target = path('sel')
    for (let level = 0; level < catalog.accessorMaxDepth; level++) target = path('table', dynamic(target))
    const { compiled } = one(target)
    expect(compiled.diagnostics).toEqual([expect.objectContaining({ firmwareError: 'ERR_VM_ACCESSOR_DEPTH_EXCEEDED' })])
  })
})

describe('compileAccessors: checks and sharing', () => {
  it.each([
    ['a position past the end', path('gains', index(4)), 'ERR_VM_ACCESSOR_OOB'],
    ['a step below a value', path('gains', index(0), index(0)), 'ERR_VM_ACCESSOR_TYPE_MISMATCH'],
    ['a missing child name', path('motor', name('torque')), 'ERR_VM_ACCESSOR_NAME_NOT_FOUND'],
    ['a name on a value', path('table', name('x')), 'ERR_VM_ACCESSOR_TYPE_MISMATCH'],
  ])('refuses %s', (_, target, firmwareError) => {
    const { compiled } = one(target)
    expect(compiled.diagnostics).toEqual([expect.objectContaining({ severity: 'error', pathKey: 'pin', firmwareError })])
  })

  it('refuses an unknown root', () => {
    expect(one(path('nope')).compiled.diagnostics).toEqual([expect.objectContaining({ severity: 'error', message: expect.stringContaining("'nope'") })])
  })

  it('shares one accessor between identical paths, however they are written', () => {
    const compiled = compile([
      { key: 'a', path: path('motor', index(1), index(2)) },
      { key: 'b', path: path('gains', index(2)) },
      { key: 'c', path: path('motor', name('gains'), index(2)) },
    ])
    expect(compiled.layout.accessors).toHaveLength(1)
    expect(compiled.layout.accessors[0]?.keys).toEqual(['a', 'b', 'c'])
  })

  it('sizes the arena: registry, accessor + 8 per step, each name + NUL', () => {
    const compiled = compile([{ key: 'a', path: path('speed') }, { key: 'b', path: path('box', name('copy'), index(0)) }], new Set(['copy']))
    // registry 2 x 4; 20; 20 + 2 x 8; 'copy' 5 → 8
    expect(compiled.arenaBytes).toBe(8 + 20 + 36 + 8)
  })
})

describe('compileProgram: accessors', () => {
  it('sends 0x44 after the values and counts the accessors in open', () => {
    const compiled = compileProgram(sample, catalog, { maxFrameBytes: 64, accessors: [{ key: 'pin', path: path('table', dynamic(path('sel'))) }] })
    expect(compiled.ok).toBe(true)
    expect(compiled.counts.accessors).toBe(2)
    expect(hex(compiled.frames[0]!).slice(0, 17)).toBe('04 41 09 00 02 00')
    expect(hex(compiled.frames.at(-1)!)).toBe('04 44 02 00 00 04 00 00 00 01 00 05 00 01 03 01 00 00')
  })
})
