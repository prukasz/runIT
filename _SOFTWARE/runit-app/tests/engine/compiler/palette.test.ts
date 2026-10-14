import { describe, expect, it } from 'vitest'
import { checkProgram } from '@engine'
import type { BlockInstance, BlockType, Field, FieldValue, Variable } from '@engine'
import { blk, engine, eno, flag, folder, num, project, read } from '../helpers'

/*
 * Every block of the palette, every variant, built with the least a user must give it and compiled: a descriptor and the
 * encoder that disagree (a field outside the state, a pin the compiler cannot wire, a wrong state size) show up here for the
 * block that has them, with no test written per block.
 */

const valueOf = (f: Field): FieldValue | undefined => {
  if (f.default !== undefined || f.fixed !== undefined) return undefined
  if (!f.required) return undefined
  if (f.options?.length) return f.options[0]!.value
  return f.min ?? 0
}

const minimal = (type: BlockType, variantId?: string): { variables: Variable[]; blocks: BlockInstance[] } => {
  const variables: Variable[] = [folder('slots', [num('slot0'), num('slot1')])]
  let n = 0
  const probe = engine.pins(blk('probe', type.id, { variant: variantId, groups: type.id === 'expr' || type.id === 'expr_bit' ? { inputs: 1 } : undefined }))!
  const inputs = probe.inputs.map((cp) => {
    const wire = cp.pin.required || cp.group !== undefined
    if (!wire) return null
    if (cp.pin.value === 'cell') return read('slots', { kind: 'index', index: 0 })
    const id = `v${n++}`
    variables.push(num(id))
    return read(id)
  })
  const values: Record<string, FieldValue> = {}
  for (const f of probe.shape.fields) {
    const v = valueOf(f)
    if (v !== undefined) values[f.id] = v
  }
  const enables = probe.shape.minEn > 0 ? [read('on')] : []
  if (enables.length) variables.push(flag('on'))

  const instance = blk('b', type.id, {
    ...(variantId ? { variant: variantId } : {}),
    ...(type.integration !== 'core' ? { device: 1 } : {}),
    ...(type.id === 'expr' || type.id === 'expr_bit' ? { groups: { inputs: 1 }, editor: { text: 'in0' } } : {}),
    inputs,
    enables,
    values,
  })
  const blocks = [instance]
  if (type.span) {
    // a loop needs a body: a block on its enable-out
    variables.push(num('x'), num('y'))
    blocks.push(blk('body', 'set', { inputs: [read('x'), read('y')], enables: [eno('b')] }))
  }
  return { variables, blocks }
}

const cases: (readonly [BlockType, string | undefined])[] = []
for (const type of engine.catalog.blocks) {
  if (type.variants.length === 0) cases.push([type, undefined])
  else for (const variant of type.variants) cases.push([type, variant.id])
}

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
