import { describe, expect, it } from 'vitest'
import { createProject, parseProject, parseWires, serializeProject } from '../../../src/domain/project'
import type { Wire } from '../../../src/domain/hardware'

const wires: Wire[] = [
  { id: 'w1', a: { device: 'd1', pin: 'SDA' }, b: { device: 'board', pin: 'i2c-sda' } },
  { id: 'w2', a: { device: 'd1', pin: 'VSS' }, b: { device: 'board', pin: 't3v3-gnd' } },
]

describe('wires in the project file', () => {
  it('round-trip in order, as references to pins and never coordinates', () => {
    const project = { ...createProject('t'), wires }
    const text = serializeProject(project)
    expect(parseProject(text).wires).toEqual(wires)
    expect(text).not.toMatch(/"x"|"y"/)
  })

  it('are left out of the file when there are none', () => {
    expect(serializeProject({ ...createProject('t'), wires: [] })).not.toContain('"wires"')
    expect(parseProject(serializeProject(createProject('t'))).wires).toBeUndefined()
  })

  it('refuse a repeated wire ID, an empty ID, and an end without a device or pin', () => {
    expect(() => parseWires([wires[0], wires[0]], 'wires')).toThrow(/used twice/)
    expect(() => parseWires([{ ...wires[0], id: '' }], 'wires')).toThrow(/empty/)
    expect(() => parseWires([{ id: 'x', a: { device: '', pin: 'p' }, b: wires[0].b }], 'wires')).toThrow(/device/)
    expect(() => parseWires([{ id: 'x', a: { device: 'd', pin: 'p' } }], 'wires')).toThrow()
  })
})
