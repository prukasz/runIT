import { describe, expect, it } from 'vitest'
import { createProject, parseBoardLayout, parseProject, serializeProject } from '../../../src/domain/project'

describe('board layout', () => {
  it('round-trips through the project file', () => {
    const project = { ...createProject('t'), boardLayout: { d1: { x: 12, y: -3.5, rotation: 270 as const, mirror: true } } }
    expect(parseProject(serializeProject(project)).boardLayout).toEqual(project.boardLayout)
  })

  it('refuses a rotation that is not a quarter turn', () => {
    expect(() => parseBoardLayout({ d1: { x: 0, y: 0, rotation: 45, mirror: false } }, 'boardLayout')).toThrow(/rotation/)
  })
})

describe('board layout pin limits', () => {
  it('keeps the voltage a user allows on a pin', () => {
    const project = { ...createProject('t'), boardLayout: { d1: { x: 0, y: 0, rotation: 0 as const, mirror: false, pinLimits: { AIN0: { mV_max: 20000 } } } } }
    expect(parseProject(serializeProject(project)).boardLayout?.d1?.pinLimits).toEqual({ AIN0: { mV_max: 20000 } })
  })
})

describe('board layout variant', () => {
  it('keeps the symbol variant', () => {
    const project = { ...createProject('t'), boardLayout: { d1: { x: 0, y: 0, rotation: 0 as const, mirror: false, variant: 'breakout' } } }
    expect(parseProject(serializeProject(project)).boardLayout?.d1?.variant).toBe('breakout')
  })
})
