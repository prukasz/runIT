import { describe, expect, it } from 'vitest'
import { createProject } from '../../domain/project'
import type { FolderNode, ObjectNode, ValueNode } from '../../domain/project'
import { arrayDims, formatChain, parseChain, resolveChain, rootCandidates } from './accessorChain'

const value = (id: string, name: string, length = 1, type = 'F'): ValueNode => ({ kind: 'value', id, name, type, length, mutable: true, retentive: false })
const folder = (id: string, name: string, children: ObjectNode[]): FolderNode => ({ kind: 'folder', id, name, children })
const project = {
  ...createProject('t'),
  objects: [
    value('v-sel', 'sel'),
    value('v-table', 'table', 6),
    folder('f-motor', 'motor', [value('v-speed', 'speed'), folder('f-gains', 'gains', [value('v-kp', 'kp'), value('v-ki', 'ki')])]),
    folder('f-m2', 'm2', [value('r0', 'row0', 4), value('r1', 'row1', 4), value('r2', 'row2', 4)]),
    folder('f-m3', 'cube', [folder('p0', 'plane0', [value('a', 'r0', 4), value('b', 'r1', 4)]), folder('p1', 'plane1', [value('c', 'r0', 4), value('d', 'r1', 4)])]),
  ],
}

describe('accessor chain text', () => {
  it('round-trips members, keys, indices and slots', () => {
    for (const text of ['sel', 'table[3]', 'motor.gains.kp', 'motor["speed"]', 'm2[1][sel]', 'cube[0][1][2]', 'table[motor.speed]', 'motor.gains["kp"][ ]']) {
      expect(formatChain(parseChain(text, project))).toBe(text)
    }
  })
  it('takes the longest dotted prefix that names an object as the root', () => {
    expect(parseChain('motor.gains.kp', project)).toEqual({ root: 'motor.gains.kp', steps: [] })
    expect(parseChain('motor.speed.extra', project)).toEqual({ root: 'motor.speed', steps: [{ kind: 'member', name: 'extra' }] })
  })
  it('reads members after brackets and nested slot paths', () => {
    expect(parseChain('cube[sel].r0[table[1]]', project).steps).toEqual([{ kind: 'index', text: 'sel' }, { kind: 'member', name: 'r0' }, { kind: 'index', text: 'table[1]' }])
  })
  it('accepts text still being typed', () => {
    expect(parseChain('m2[', project)).toEqual({ root: 'm2', steps: [{ kind: 'index', text: '' }] })
    expect(parseChain('motor.', project).root).toBe('motor')
  })
})

describe('accessor chain meaning', () => {
  it('finds 1D, 2D and 3D shapes', () => {
    expect(arrayDims(project.objects[1])).toEqual([6])
    expect(arrayDims(project.objects[3])).toEqual([3, 4])
    expect(arrayDims(project.objects[4])).toEqual([2, 2, 4])
    expect(arrayDims(project.objects[2])).toBeUndefined()
  })
  it('follows steps through folders and arrays', () => {
    expect(resolveChain(project, parseChain('motor.gains["kp"]', project)).node?.name).toBe('kp')
    expect(resolveChain(project, parseChain('cube[1][0]', project))).toMatchObject({ ok: true, indexed: false })
    expect(resolveChain(project, parseChain('cube[1][0][3]', project))).toMatchObject({ ok: true, indexed: true })
    expect(resolveChain(project, parseChain('table[sel]', project))).toMatchObject({ ok: true, indexed: true })
  })
  it('says why a chain leads nowhere', () => {
    expect(resolveChain(project, parseChain('motor["nope"]', project)).reason).toMatch(/not in/)
    expect(resolveChain(project, parseChain('table[9]', project)).reason).toMatch(/6 elements/)
    expect(resolveChain(project, parseChain('table[1][2]', project)).ok).toBe(false)
  })
  it('lists arrays of any depth with one slot per dimension', () => {
    const cube = rootCandidates(project).find((entry) => entry.fullPath === 'cube')!
    expect(cube.suffix).toBe('[ ][ ][ ]')
    expect(rootCandidates(project).find((entry) => entry.fullPath === 'motor')!.suffix).toBe('')
  })
})
