import { describe, expect, it } from 'vitest'
import {
  appendStep,
  cutSteps,
  dimensionsOf,
  draftForVariable,
  EMPTY_DRAFT,
  formatDraft,
  inspectDraft,
  nextSteps,
  parseDraft,
  parsePath,
  positionOptions,
  replaceStep,
  rootOptions,
  setRoot,
  shapeText,
  toPath,
} from '@engine/model'
import type { PathDraft, Variable } from '@engine/model'

const value = (id: string, name: string, type: 'bool' | 'u8' | 'u32' | 'i32' | 'f32' | 'str' = 'f32', extra: object = {}): Variable => ({ kind: 'value', id, name, type, count: 1, ...extra })
const folder = (id: string, name: string, children: Variable[]): Variable => ({ kind: 'folder', id, name, children })

const tree: Variable[] = [
  value('sel', 'sel', 'u8'),
  value('speed', 'speed'),
  value('title', 'title', 'str', { count: 8 }),
  value('table', 'table', 'u8', { count: 4 }),
  folder('motors', 'motors', [value('left', 'left', 'f32', { count: 3 }), value('right', 'right', 'f32', { count: 3 })]),
  folder('grid', 'grid', [value('r0', 'r0', 'f32', { count: 4 }), value('r1', 'r1', 'f32', { count: 4 }), value('r2', 'r2', 'f32', { count: 4 })]),
  folder('cube', 'cube', [
    folder('p0', 'p0', [value('a0', 'a0', 'f32', { count: 4 }), value('a1', 'a1', 'f32', { count: 4 })]),
    folder('p1', 'p1', [value('b0', 'b0', 'f32', { count: 4 }), value('b1', 'b1', 'f32', { count: 4 })]),
  ]),
  folder('mixed', 'mixed', [value('m1', 'm1'), value('m2', 'm2', 'u8')]),
  folder('front', 'front left', [value('x', 'x')]),
  { kind: 'reference', id: 'alias', name: 'alias', target: 'speed' },
]

const draft = (text: string): PathDraft => parseDraft(tree, text).draft
const text = (d: PathDraft) => formatDraft(tree, d)

describe('draft: text in, text out', () => {
  it('reads what the compiler reads, and writes it back', () => {
    for (const source of ['speed', 'motors.left[2]', 'table[sel]', 'grid[1][2]', 'cube.p1.b0[3]']) {
      const parsed = parseDraft(tree, source)
      expect(parsed.problem, source).toBeUndefined()
      expect(toPath(parsed.draft), source).toEqual((parsePath(tree, source) as { path: unknown }).path)
      expect(text(parsed.draft)).toBe(formatPathOf(source))
    }
  })

  const formatPathOf = (source: string) => text(draft(source))

  it('reads what is still being typed: an open bracket, a dot, a half name', () => {
    expect(parseDraft(tree, 'motors.').draft.steps).toEqual([])
    expect(parseDraft(tree, 'motors.').problem?.message).toMatch(/after the dot/)
    expect(parseDraft(tree, 'table[').draft.steps).toEqual([{ kind: 'dynamic', path: null }])
    expect(parseDraft(tree, 'table[]').draft.steps).toEqual([{ kind: 'dynamic', path: null }])
    expect(parseDraft(tree, 'table[se').draft.steps).toEqual([{ kind: 'dynamic', path: null }]) // 'se' is no variable yet
    expect(parseDraft(tree, 'table[se').problem?.message).toMatch(/no variable called 'se'/)
    expect(parseDraft(tree, 'table[sel').draft.steps).toEqual([{ kind: 'dynamic', path: { root: 'sel', steps: [] } }])
  })

  it('keeps the steps before the first mistake and says where it is', () => {
    const r = parseDraft(tree, 'motors.left;')
    expect(r.draft.steps).toHaveLength(1)
    expect(r.problem).toEqual({ message: "Unexpected ';'.", at: 11 })
    expect(parseDraft(tree, 'nothing').draft).toEqual(EMPTY_DRAFT)
    expect(parseDraft(tree, 'nothing').problem?.at).toBe(0)
    expect(parseDraft(tree, '').problem).toBeUndefined()
  })

  it('writes an empty slot as [] and a name that needs quotes quoted', () => {
    expect(text(draft('table['))).toBe('table[]')
    expect(text(draft('front["x"]'.replace('front', '["front left"]')))).toBe('["front left"].x')
  })

  it('is complete only without empty slots', () => {
    expect(toPath(draft('table['))).toBeUndefined()
    expect(toPath(draft('table[2]'))).toEqual({ root: 'table', steps: [{ kind: 'index', index: 2 }] })
    expect(toPath(EMPTY_DRAFT)).toBeUndefined()
  })
})

describe('draft: editing returns a new draft', () => {
  it('appends, replaces, cuts, and changes the root', () => {
    const base = draft('motors')
    const a = appendStep(base, { kind: 'name', name: 'left' }, { kind: 'dynamic', path: null })
    expect(text(a)).toBe('motors.left[]')
    expect(text(replaceStep(a, 1, { kind: 'index', index: 2 }))).toBe('motors.left[2]')
    expect(text(cutSteps(a, 1))).toBe('motors.left')
    expect(text(base)).toBe('motors') // the original is untouched
    expect(setRoot(a, 'motors')).toBe(a)
    expect(setRoot(a, 'speed')).toEqual({ root: 'speed', steps: [] })
  })

  it('builds the draft that names a variable anywhere in the tree', () => {
    expect(text(draftForVariable(tree, 'right')!)).toBe('motors.right')
    expect(text(draftForVariable(tree, 'speed')!)).toBe('speed')
    expect(draftForVariable(tree, 'nope')).toBeUndefined()
  })
})

describe('draft: what it names', () => {
  it('names a variable, an element, or fails with the reason', () => {
    const r = inspectDraft(tree, draft('motors.left[1]'))
    expect(r.complete).toBe(true)
    expect(r.resolution).toMatchObject({ ok: true, target: { element: 1, type: 'f32' } })
    const bad = inspectDraft(tree, draft('table[9]'))
    expect(bad.resolution).toMatchObject({ ok: false, code: 'ERR_VM_ACCESSOR_OOB' })
    expect(inspectDraft(tree, EMPTY_DRAFT).resolution).toBeUndefined()
  })

  it('lets an empty slot stand for the first entry and says which are empty', () => {
    const r = inspectDraft(tree, draft('motors['))
    expect(r.complete).toBe(false)
    expect(r.empty).toEqual([0])
    expect(r.resolution).toMatchObject({ ok: true, target: { variable: { id: 'left' } } })
  })
})

describe('draft: what can follow', () => {
  it('a folder offers its children, by name and by position', () => {
    const next = nextSteps(tree, draft('motors'))
    if (next.kind !== 'children') throw new Error(next.kind)
    expect(next.children.map((c) => [c.variable.name, c.byName, c.byIndex])).toEqual([
      ['left', { kind: 'name', name: 'left' }, { kind: 'index', index: 0 }],
      ['right', { kind: 'name', name: 'right' }, { kind: 'index', index: 1 }],
    ])
    expect(next.dimensions).toEqual([2, 3])
  })

  it('an array offers its positions, an element or a scalar nothing', () => {
    expect(nextSteps(tree, draft('table'))).toMatchObject({ kind: 'elements', count: 4 })
    expect(nextSteps(tree, draft('motors.left'))).toMatchObject({ kind: 'elements', count: 3 })
    expect(nextSteps(tree, draft('table[1]'))).toEqual({ kind: 'none' })
    expect(nextSteps(tree, draft('speed'))).toEqual({ kind: 'none' })
    expect(nextSteps(tree, draft('title'))).toEqual({ kind: 'none' }) // text is one value
    expect(nextSteps(tree, EMPTY_DRAFT)).toEqual({ kind: 'none' })
    expect(nextSteps(tree, draft('table[9]'))).toEqual({ kind: 'none' })
  })

  it('a reference offers what it points to', () => {
    const next = nextSteps(tree, draft('alias'))
    expect(next).toMatchObject({ kind: 'children' })
  })

  it('goes on after a slot', () => {
    expect(nextSteps(tree, draft('cube[]'))).toMatchObject({ kind: 'children', of: { id: 'p0' } })
  })
})

describe('shapes', () => {
  it('measures arrays of one, two and three dimensions, and says nothing for the rest', () => {
    const dims = (id: string) => dimensionsOf(tree, tree.find((v) => v.id === id)!)
    expect(dims('table')).toEqual([4])
    expect(dims('grid')).toEqual([3, 4])
    expect(dims('cube')).toEqual([2, 2, 4])
    expect(dims('mixed')).toBeUndefined() // children differ
    expect(dims('speed')).toBeUndefined()
    expect(dims('title')).toBeUndefined()
  })

  it('reads in a list', () => {
    const shape = (id: string) => shapeText(tree, tree.find((v) => v.id === id)!)
    expect([shape('table'), shape('grid'), shape('cube'), shape('mixed'), shape('speed')]).toEqual(['4 items', '2D 3×4', '3D 2×2×4', 'folder · 2', ''])
  })
})

describe('what to offer', () => {
  it('roots: the top-level variables, with a slot per dimension for an array of folders', () => {
    const roots = rootOptions(tree)
    expect(roots.map((r) => r.variable.id)).toEqual(tree.map((v) => v.id))
    const grid = roots.find((r) => r.variable.id === 'grid')!
    expect([grid.suffix, grid.shape]).toEqual(['[][]', '2D 3×4'])
    expect(roots.find((r) => r.variable.id === 'speed')!.suffix).toBe('')
  })

  it('positions: single numbers anywhere in the tree, filtered by what is typed', () => {
    const all = positionOptions(tree, '', 100).map((o) => o.text)
    expect(all).toContain('sel')
    expect(all).toContain('mixed.m1')
    expect(all).not.toContain('title') // text
    expect(all).not.toContain('table') // an array
    expect(positionOptions(tree, 'SE').map((o) => o.text)).toEqual(['sel'])
    expect(positionOptions(tree, 'sel')).toEqual([]) // already exactly that
    expect(positionOptions(tree, '', 2)).toHaveLength(2)
  })

  it('a position option is a path the draft takes', () => {
    const [option] = positionOptions(tree, 'sel')
    expect(option).toBeUndefined()
    const pick = positionOptions(tree, 'se')[0]!
    const d = replaceStep(draft('table['), 0, { kind: 'dynamic', path: pick.path })
    expect(text(d)).toBe('table[sel]')
    expect(inspectDraft(tree, d).complete).toBe(true)
  })
})
