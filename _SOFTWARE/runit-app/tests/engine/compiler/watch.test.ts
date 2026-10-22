import { describe, expect, it } from 'vitest'
import { doneKey, outputKey, variableKey, watchPlan } from '@engine'
import { blk, engine, flag, folder, num, project, read } from '../helpers'

/** `z = x * 2 + y` once `on` is true, with `x` and `y` in a folder: one block, a flow flag and a folder of values. */
const sample = () => {
  const p = project(
    [folder('in', [num('x'), num('y')]), num('z'), flag('on')],
    [blk('e', 'expr', { groups: { inputs: 2 }, editor: { text: 'in0 * 2 + in1' }, inputs: [read('in', { kind: 'name', name: 'x' }), read('in', { kind: 'name', name: 'y' })], enables: [read('on')], outputs: [{ path: { root: 'z', steps: [] } }] })],
  )
  return engine.compile(p, { allEnableOuts: true })
}

describe('the telemetry a live view asks for', () => {
  it('follows the flow and the variables, a folder as one id for its children', () => {
    const result = sample()
    expect(result.ok, JSON.stringify(result.diagnostics)).toBe(true)
    const plan = watchPlan(result, 'flow')
    const keys = [...plan.targets.values()].flat()
    expect(keys).toContain(doneKey('e'))
    for (const id of ['in', 'z', 'on']) expect(keys).toContain(variableKey(id))
    // x and y are reached through their folder: they are targets, but not ids of their own
    const idOf = (variable: string) => result.ids.variables.get(variable)!
    expect(plan.ids).toContain(idOf('in'))
    expect(plan.ids).not.toContain(idOf('x'))
    expect(plan.dropped).toBe(0)
    // the block's output is the variable z: both names go to one object
    expect(plan.targets.get(idOf('z'))).toEqual(expect.arrayContaining([variableKey('z'), outputKey('e', 0)]))
  })

  it("watches a block's own output in the live view too, after the variables", () => {
    const p = project([num('x'), flag('on')], [blk('e', 'expr', { groups: { inputs: 1 }, editor: { text: 'in0 * 2' }, inputs: [read('x')], enables: [read('on')] })])
    const result = engine.compile(p, { allEnableOuts: true })
    expect(result.ok, JSON.stringify(result.diagnostics)).toBe(true)
    const own = result.ids.outputs.get(outputKey('e', 0))!
    const flow = watchPlan(result, 'flow')
    expect(flow.ids).toContain(own)
    expect(flow.targets.get(own)).toEqual([outputKey('e', 0)])
    expect(flow.ids.indexOf(own)).toBeGreaterThan(flow.ids.indexOf(result.ids.variables.get('x')!))
  })

  it("drops what the board's limits do not let it follow, the flow first", () => {
    const result = sample()
    const plan = watchPlan(result, 'flow', { ...engine.limits, subscribeMax: 1 })
    expect(plan.ids).toHaveLength(1)
    expect(plan.targets.get(plan.ids[0]!)).toContain(doneKey('e'))
    expect(plan.dropped).toBeGreaterThan(0)
    expect(watchPlan(result, 'flow', { ...engine.limits, subscribeTracked: 2 }).ids.length).toBeLessThanOrEqual(2)
  })
})
