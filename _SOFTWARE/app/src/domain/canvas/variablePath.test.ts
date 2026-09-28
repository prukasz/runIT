import { describe, expect, it } from 'vitest'
import { createProject } from '../project'
import type { ProjectDocument, ValueNode } from '../project'
import { chipLabel, nameIds, parsePathText, pathLabel } from '.'

const value = (id: string, name: string): ValueNode => ({ kind: 'value', id, name, type: 'F', length: 4, mutable: true, retentive: false })
const project: ProjectDocument = {
  ...createProject('paths'),
  objects: [
    { kind: 'folder', id: 'f1', name: 'motor', children: [value('g1', 'gains'), value('t1', 'temperature')] },
    value('sel1', 'sel'),
    value('tab1', 'table'),
  ],
}

describe('variable paths as names', () => {
  it('shows a path by names, and a long one by its end', () => {
    expect(pathLabel({ root: 'g1', steps: [{ kind: 'index', index: 2 }] }, project)).toBe('motor.gains[2]')
    expect(pathLabel({ root: 'tab1', steps: [{ kind: 'dynamic', index: { root: 'sel1' } }] }, project)).toBe('table[sel]')
    expect(pathLabel({ root: 'if1:q0' }, project)).toBe('if1:q0')
    expect(chipLabel('motor.temperature')).toBe('…temperature')
    expect(chipLabel('sel')).toBe('sel')
    expect(chipLabel('a.averyveryverylongname')).toBe('…eryverylongname')
  })

  it('names the objects a message quotes by ID, leaving other quotes alone', () => {
    expect(nameIds("Block 'expr1': 'g1' is written twice, 'if1:q0' too.", project)).toBe("Block 'expr1': 'motor.gains' is written twice, 'if1:q0' too.")
    expect(nameIds("'g1'", undefined)).toBe("'g1'")
  })

  it('parses what the user types back into a path', () => {
    expect(parsePathText('motor.gains[2]', project)).toEqual({ root: 'g1', steps: [{ kind: 'index', index: 2 }] })
    expect(parsePathText('temperature', project)).toEqual({ root: 't1' })
    expect(parsePathText('table[sel]', project)).toEqual({ root: 'tab1', steps: [{ kind: 'dynamic', index: { root: 'sel1' } }] })
    expect(parsePathText('motor.gains.kp', project)).toEqual({ root: 'g1', steps: [{ kind: 'name', name: 'kp' }] })
    expect(parsePathText('if1:q0', project)).toEqual({ root: 'if1:q0' })
    expect(() => parsePathText('nothing', project)).toThrow(/No variable is called 'nothing'/)
    expect(() => parsePathText('table[sel', project)).toThrow(/missing/)
  })
})
