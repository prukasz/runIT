import { describe, expect, it } from 'vitest'
import { parseChain, resolveChain, rootCandidates } from '../../src/blocks/accessorChain'
import { parsePathText, pathLabel, PathTextError } from '../../src/domain/canvas'
import { createProject } from '../../src/domain/project'
import type { ProjectDocument } from '../../src/domain/project'

const project: ProjectDocument = {
  ...createProject('t'),
  objects: [
    { kind: 'value', id: 'obj-speed', name: 'speed', type: 'F', length: 1, mutable: true, retentive: false },
    { kind: 'value', id: 'obj-gains', name: 'gains', type: 'F', length: 4, mutable: true, retentive: false },
    { kind: 'folder', id: 'obj-motor', name: 'motor', children: [{ kind: 'value', id: 'obj-kp', name: 'kp', type: 'F', length: 1, mutable: true, retentive: false }] },
  ],
}

describe('variable paths as text', () => {
  it('reads names, folders and positions', () => {
    expect(parsePathText('speed', project)).toEqual({ root: 'obj-speed' })
    // A child of a folder is an object of its own: its name path finds it.
    expect(parsePathText('motor.kp', project)).toEqual({ root: 'obj-kp' })
    expect(parsePathText('motor.live', project)).toEqual({ root: 'obj-motor', steps: [{ kind: 'name', name: 'live' }] })
    expect(parsePathText('gains[2]', project)).toEqual({ root: 'obj-gains', steps: [{ kind: 'index', index: 2 }] })
    expect(parsePathText('gains[speed]', project)).toEqual({ root: 'obj-gains', steps: [{ kind: 'dynamic', index: { root: 'obj-speed' } }] })
  })

  it('reads a block pin as it is stored', () => {
    expect(parsePathText('periodic1:q0', project)).toEqual({ root: 'periodic1:q0' })
  })

  it('says what is wrong', () => {
    expect(() => parsePathText('nothing', project)).toThrow(PathTextError)
    expect(() => parsePathText('gains[', project)).toThrow(/']'/)
  })

  it('writes a path back as names', () => {
    expect(pathLabel({ root: 'obj-motor', steps: [{ kind: 'name', name: 'kp' }] }, project)).toBe('motor.kp')
    expect(pathLabel({ root: 'obj-gains', steps: [{ kind: 'index', index: 2 }] }, project)).toBe('gains[2]')
  })
})

describe('the accessor chain', () => {
  it('follows folders and arrays', () => {
    expect(resolveChain(project, parseChain('motor.kp', project)).ok).toBe(true)
    expect(resolveChain(project, parseChain('gains[3]', project))).toMatchObject({ ok: true, indexed: true })
    expect(resolveChain(project, parseChain('gains[9]', project)).ok).toBe(false)
    expect(resolveChain(project, parseChain('motor.nope', project)).missing).toBe('member')
  })

  it('lists what a pin can start from, with a slot per dimension', () => {
    const roots = rootCandidates(project)
    expect(roots.find((entry) => entry.name === 'gains')?.suffix).toBe('[ ]')
    expect(roots.find((entry) => entry.name === 'speed')?.suffix).toBe('')
  })
})
