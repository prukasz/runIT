import { describe, expect, it } from 'vitest'
import type { Project, WireFrame } from '@engine'
import { blk, build, engine, eno, flag, folder, num, out, project, read } from '../helpers'
import { cases, minimal } from '../minimal'

/* The frames of a compiled project as a board would hand them back. */
const stored = (p: Project): WireFrame[] => build(p).frames.map((f) => ({ op: f.op, payload: f.payload }))

/** Compile, recover, and require the recovered project to compile to the very same program. */
const roundTrip = (p: Project) => {
  const recovered = engine.recover(stored(p))
  expect(recovered.diagnostics.filter((d) => d.severity === 'error'), JSON.stringify(recovered.diagnostics)).toEqual([])
  expect(recovered.parsed.problems).toEqual([])
  expect(recovered.compileDiagnostics.filter((d) => d.severity === 'error'), JSON.stringify(recovered.compileDiagnostics)).toEqual([])
  expect(recovered.exact, 'the recovered project compiles to the same program').toBe(true)
  return recovered
}

describe('every block of the palette comes back from its program', () => {
  for (const [type, variantId] of cases) {
    const variant = type.variants.find((v) => v.id === variantId)
    if (variant?.unsupported) continue
    it(`${type.id}${variantId ? `.${variantId}` : ''}`, () => {
      const { variables, blocks } = minimal(type, variantId)
      const recovered = roundTrip(project(variables, blocks))
      const block = recovered.project.blocks.find((b) => b.type === type.id)
      expect(block, 'the block is in the recovered project').toBeDefined()
      if (variantId) expect(block!.variant).toBe(variantId)
    })
  }
})

describe('variables come back as they were laid out', () => {
  const tree = () => [
    num('speed', 'speed', { initial: 2.5, retained: true }),
    flag('run', 'run', { protected: true }),
    { kind: 'value', id: 'title', name: 'title', type: 'str', count: 8, initial: 'hello' } as const,
    num('table', 'table', { count: 4, initial: [1, 2, 3, 4] }),
    num('const', 'const', { writable: false, initial: 9 }),
    folder('motors', [num('left', 'left', { count: 3 }), num('right', 'right', { count: 3 }), folder('inner', [num('deep')])]),
    { kind: 'reference', id: 'alias', name: 'alias', target: 'speed' } as const,
  ]

  it('values with their type, count, initial value and flags; folders; a reference', () => {
    const recovered = roundTrip(project(tree(), []))
    const v = recovered.project.variables
    expect(v.map((x) => [x.kind, x.name])).toEqual([['value', 'speed'], ['value', 'run'], ['value', 'title'], ['value', 'table'], ['value', 'const'], ['folder', 'motors'], ['reference', 'alias']])
    expect(v[0]).toMatchObject({ type: 'f32', initial: 2.5, retained: true })
    expect(v[1]).toMatchObject({ type: 'bool', protected: true })
    expect(v[2]).toMatchObject({ type: 'str', count: 8, initial: 'hello' })
    expect(v[3]).toMatchObject({ count: 4, initial: [1, 2, 3, 4] })
    expect(v[4]).toMatchObject({ writable: false, initial: 9 })
    const motors = v[5] as Extract<(typeof v)[number], { kind: 'folder' }>
    expect(motors.children.map((c) => c.name)).toEqual(['left', 'right', 'inner'])
    expect((motors.children[2] as typeof motors).children.map((c) => c.name)).toEqual(['deep'])
    expect(v[6]).toMatchObject({ kind: 'reference', target: v[0]!.id })
  })

  it('a reference to something that follows it, but not at once, is still a reference', () => {
    const recovered = roundTrip(project([{ kind: 'reference', id: 'r', name: 'r', target: 'later' }, num('mid'), num('later')], []))
    expect(recovered.project.variables.map((x) => x.kind)).toEqual(['reference', 'value', 'value'])
  })

  it('a reference to the very next object is a folder of one: the program is the same, so the board cannot tell', () => {
    const recovered = roundTrip(project([{ kind: 'reference', id: 'r', name: 'r', target: 'later' }, num('later')], []))
    expect(recovered.project.variables.map((x) => x.kind)).toEqual(['folder'])
  })

  it('an unset reference and an empty program', () => {
    roundTrip(project([{ kind: 'reference', id: 'r', name: 'r', target: null }], []))
    roundTrip(project([], []))
  })
})

describe('blocks come back wired', () => {
  it('a formula, with its text; the variable an output also writes; blocks reading blocks', () => {
    const p = project(
      [num('x'), num('y', 'y', { initial: 3 }), num('z')],
      [
        blk('e', 'expr', { groups: { inputs: 2 }, inputs: [read('x'), read('y')], outputs: [{ path: { root: 'z', steps: [] } }], editor: { text: 'in0 * 2 + (in1 - 1)' } }),
        blk('f', 'expr', { groups: { inputs: 1 }, inputs: [out('e')], editor: { text: 'sqrt(abs(in0)) > 2 ? 1 : 0' } }),
        blk('g', 'expr', { groups: { inputs: 1 }, inputs: [out('f')], editor: { text: 'in0 + 1' } }),
      ],
    )
    const recovered = roundTrip(p)
    const [e, f, g] = recovered.project.blocks
    expect(e).toMatchObject({ type: 'expr', groups: { inputs: 2 }, editor: { text: 'in0 * 2 + (in1 - 1)' } })
    expect(e!.outputs[0]).toEqual({ path: { root: 'v2', steps: [] } })
    // e writes the variable z: what reads e's output reads z (the program holds the same wire either way)
    expect(f!.inputs[0]).toEqual({ kind: 'variable', path: { root: 'v2', steps: [] } })
    expect(f).toMatchObject({ editor: { text: 'sqrt(abs(in0)) > 2 ? 1 : 0' } })
    // f's output is the block's own object: it comes back as a wire from the block
    expect(g!.inputs[0]).toEqual({ kind: 'output', block: f!.id, pin: 0 })
  })

  it('enables and enable-outs, an error policy, an enable mode', () => {
    const p = project(
      [flag('go'), num('a'), num('b')],
      [
        blk('p', 'periodic', { values: { period: 250 }, enables: [read('go')] }),
        blk('s', 'set', { inputs: [read('a'), read('b')], enables: [eno('p')], enableMode: 'all', onError: 'stop' }),
      ],
    )
    const recovered = roundTrip(p)
    const [periodic, set] = recovered.project.blocks
    expect(periodic).toMatchObject({ type: 'periodic', values: { period: 250 } })
    expect(periodic!.enables).toEqual([{ kind: 'variable', path: { root: 'v0', steps: [] } }])
    expect(set).toMatchObject({ enableMode: 'all', onError: 'stop' })
    expect(set!.enables).toEqual([{ kind: 'eno', block: periodic!.id }])
  })

  it('a device block: the device, the operation and its arguments', () => {
    const p = project([], [blk('t', 'io', { variant: 'set_level', device: 1, values: { pin: 22, level: true } })])
    const recovered = roundTrip(p)
    expect(recovered.project.blocks[0]).toMatchObject({ type: 'io', variant: 'set_level', device: 1, values: { pin: 22 } })
  })

  it('a loop and its body, paths into arrays and folders, a position read from a variable', () => {
    const p = project(
      [num('table', 'table', { count: 4 }), num('i'), folder('motors', [num('left', 'left', { count: 3 }), num('right', 'right', { count: 3 })]), num('out')],
      [
        blk('c', 'set', { inputs: [read('motors', { kind: 'name', name: 'left' }, { kind: 'index', index: 2 }), read('out')] }),
        blk('d', 'set', { inputs: [read('table', { kind: 'dynamic', path: { root: 'i', steps: [] } }), read('out')] }),
        blk('l', 'for', { values: { op: 0 } }),
        blk('body', 'set', { inputs: [read('i'), read('out')], enables: [eno('l')] }),
      ],
    )
    const recovered = roundTrip(p)
    const [c, d] = recovered.project.blocks
    expect(c!.inputs[0]).toEqual({ kind: 'variable', path: { root: 'v2', steps: [{ kind: 'name', name: 'left' }, { kind: 'index', index: 2 }] } })
    expect(d!.inputs[0]).toMatchObject({ kind: 'variable', path: { root: 'v0', steps: [{ kind: 'dynamic', path: { root: 'v1' } }] } })
  })
})

describe('what the board hands back can be wrong', () => {
  it('says what it cannot read instead of failing: an unknown block type, an unknown operation, a cut frame', () => {
    const frames = stored(project([num('x')], [blk('e', 'expr', { groups: { inputs: 1 }, inputs: [read('x')], editor: { text: 'in0' } })]))
    const last = frames[frames.length - 1]!
    const unknownType = frames.slice(0, -1).concat({ op: last.op, payload: Uint8Array.from(last.payload, (b, i) => (i === 4 ? 0xee : b)) })
    expect(engine.recover(unknownType).diagnostics.map((d) => d.code)).toContain('unknown-block')
    const cut = frames.slice(0, -1).concat({ op: last.op, payload: last.payload.subarray(0, 6) })
    expect(engine.recover(cut).parsed.problems.join(' ')).toMatch(/cut short/)
    const device = stored(project([], [blk('t', 'io', { variant: 'toggle', device: 1, values: { pin: 5 } })]))
    const lastDevice = device[device.length - 1]!
    const badOp = device.slice(0, -1).concat({ op: lastDevice.op, payload: Uint8Array.from(lastDevice.payload, (b, i) => (i === lastDevice.payload.length - 56 + 2 ? 0x77 : b)) })
    expect(engine.recover(badOp).diagnostics.map((d) => d.code)).toContain('unknown-operation')
  })

  it('ignores the frames after the load (EXEC, SUBSCRIBE) and tells which', () => {
    const frames = [...stored(project([num('x')], [])), { op: engine.exec('normal').op, payload: engine.exec('normal').payload }]
    const recovered = engine.recover(frames)
    expect(recovered.parsed.exec).toEqual(['normal'])
    expect(recovered.exact).toBe(true)
    const withSubscribe = [...frames, engine.subscribe([0])]
    expect(engine.recover(withSubscribe).parsed.problems.join(' ')).toMatch(/not part of a load/)
  })
})
