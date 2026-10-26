import { describe, expect, it } from 'vitest'
import { engine, project } from '../helpers'

describe('the engine over the bundled descriptors', () => {
  it('compiles an empty project into nothing to load', () => {
    const result = engine.compile(project([], []))
    expect(result.ok).toBe(true)
    expect(result.diagnostics).toEqual([])
    expect(result.order).toEqual([])
  })

  it('knows the whole palette', () => {
    expect(engine.catalog.blocks).toHaveLength(24)
    expect(engine.catalog.warnings).toEqual([])
  })
})
