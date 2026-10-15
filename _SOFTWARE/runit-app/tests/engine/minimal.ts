import type { BlockInstance, BlockType, Field, FieldValue, Variable } from '@engine'
import { blk, engine, eno, flag, folder, num, read } from './helpers'

/*
 * The least a user must give a block (and a variant of it) for it to compile: the projects the palette test compiles and the
 * recovery test reads back.
 */

const valueOf = (f: Field): FieldValue | undefined => {
  if (f.default !== undefined || f.fixed !== undefined) return undefined
  if (!f.required) return undefined
  if (f.options?.length) return f.options[0]!.value
  return f.min ?? 0
}

export const minimal = (type: BlockType, variantId?: string): { variables: Variable[]; blocks: BlockInstance[] } => {
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

export const cases: (readonly [BlockType, string | undefined])[] = []
for (const type of engine.catalog.blocks) {
  if (type.variants.length === 0) cases.push([type, undefined])
  else for (const variant of type.variants) cases.push([type, variant.id])
}
