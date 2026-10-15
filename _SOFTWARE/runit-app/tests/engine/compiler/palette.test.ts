import { describe, expect, it } from 'vitest'
import { checkProgram } from '@engine'
import { engine, project } from '../helpers'
import { cases, minimal } from '../minimal'

/*
 * Every block of the palette, every variant, built with the least a user must give it and compiled: a descriptor and the
 * encoder that disagree (a field outside the state, a pin the compiler cannot wire, a wrong state size) show up here for the
 * block that has them, with no test written per block.
 */

describe('every block of the palette compiles with the least a user must give it', () => {
  for (const [type, variantId] of cases) {
    const variant = type.variants.find((v) => v.id === variantId)
    const name = `${type.id}${variantId ? `.${variantId}` : ''}`

    if (variant?.unsupported) {
      it(`${name}: is refused with the reason, not guessed`, () => {
        const { variables, blocks } = minimal(type, variantId)
        const result = engine.compile(project(variables, blocks))
        expect(result.ok).toBe(false)
        expect(result.diagnostics.map((d) => d.code)).toContain('variant-unsupported')
      })
      continue
    }

    it(`${name}`, () => {
      const { variables, blocks } = minimal(type, variantId)
      const result = engine.compile(project(variables, blocks))
      expect(result.diagnostics.filter((d) => d.severity === 'error'), JSON.stringify(result.diagnostics)).toEqual([])
      expect(result.ok).toBe(true)
      expect(checkProgram(result.ir!)).toEqual([])
      const block = result.ir!.blocks.find((b) => b.type === type.wireId)!
      expect(block, 'the block is in the program').toBeDefined()
      if (typeof type.stateLength === 'number') expect(block.state.length).toBeGreaterThanOrEqual(type.stateLength)
    })
  }

  it('covers every block of the catalog', () => {
    expect(new Set(cases.map(([type]) => type.id))).toEqual(new Set(engine.catalog.blocks.map((b) => b.id)))
  })
})
