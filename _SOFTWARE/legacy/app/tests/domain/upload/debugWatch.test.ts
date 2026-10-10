import { describe, expect, it } from 'vitest'
import { formatLiveValue, readPath, truthy } from '../../../src/domain/compiler'
import type { LiveValues } from '../../../src/domain/compiler'
import { runitVmCatalog } from '../../../src/domain/descriptors'
import { createProject } from '../../../src/domain/project'
import type { CanvasBlock, ObjectPath, ProjectCanvas, ProjectDocument, ValueNode } from '../../../src/domain/project'
import { blockLive, nextBlockIndex, planVmUpload, subscribeRoom, withWatched } from '../../../src/domain/upload'

const vm = runitVmCatalog()
const value = (id: string, type: string, initial: ValueNode['value'], length = 1): ValueNode => ({ kind: 'value', id, name: id, type, length, value: initial, mutable: true, retentive: false })
const at = (root: string): ObjectPath => ({ root })
const tick = at('every:eno')
const block = (extra: Omit<CanvasBlock, 'x' | 'y'>): CanvasBlock => ({ x: 0, y: 0, ...extra })

const canvas: ProjectCanvas = {
  id: 'c1',
  name: 'Canvas 1',
  blocks: [
    block({ id: 'every', type: 'PERIODIC', settings: { period: 100, time_base: 'MS' } }),
    block({ id: 'cnt', type: 'EXPR', inputs: [at('n')], outputs: ['n'], enables: [tick], expression: { constants: [1], code: ['in', 0, 'const', 0, '+'] } }),
    block({ id: 'pick', type: 'EXPR', inputs: [{ root: 'table', steps: [{ kind: 'dynamic', index: at('sel') }] }], enables: [tick], expression: { code: ['in', 0] } }),
  ],
}
const project: ProjectDocument = { ...createProject('debug'), objects: [value('n', 'F', [0]), value('sel', 'U8', [1]), value('table', 'F', [10, 20, 30], 3)], canvases: [canvas] }

const f32 = (...numbers: number[]): Uint8Array => new Uint8Array(new Float32Array(numbers).buffer)

describe('debug upload plan', () => {
  const plan = planVmUpload(project, vm, { maxFrameBytes: 240, debug: true })

  it('gives every block an ENO object and subscribes to it', () => {
    expect(plan.ok).toBe(true)
    expect(plan.debug?.blocks.size).toBe(3)
    for (const watch of plan.debug!.blocks.values()) {
      expect(watch.eno).toBeDefined()
      expect(plan.subscribed).toContain(watch.eno)
    }
  })

  it('subscribes to what the blocks write, are gated by and read, dynamic positions included', () => {
    const wire = (id: string) => plan.program.objects.wireIdOf.get(id)!
    expect(plan.subscribed).toEqual(expect.arrayContaining([wire('n'), wire('sel'), wire('table'), wire('every:eno'), wire('pick:q0')]))
    expect([...plan.subscribed]).toEqual([...plan.subscribed].sort((a, b) => a - b))
  })

  it('sends the normal build no ENO objects nobody reads', () => {
    const normal = planVmUpload(project, vm, { maxFrameBytes: 240 })
    expect(normal.debug).toBeUndefined()
    expect(normal.program.counts.objects).toBeLessThan(plan.program.counts.objects)
  })

  it('reads a path live, following a dynamic position', () => {
    const layout = plan.program.objects
    const wire = (id: string) => layout.wireIdOf.get(id)!
    const path: ObjectPath = { root: 'table', steps: [{ kind: 'dynamic', index: at('sel') }] }
    const live = new Map<number, Uint8Array>([[wire('table'), f32(10, 20, 30)]])
    expect(readPath(layout, live, path)).toBeUndefined()
    const withSel: LiveValues = new Map(live).set(wire('sel'), Uint8Array.of(2))
    expect(readPath(layout, withSel, path)).toBe(30)
    expect(readPath(layout, new Map(withSel).set(wire('sel'), Uint8Array.of(0)), path)).toBe(10)
    expect(readPath(layout, live, at('table'))).toEqual([10, 20, 30])
  })

  it('writes values short and treats numbers as gates', () => {
    expect(formatLiveValue(3.14159265)).toBe('3.142')
    expect(formatLiveValue(42)).toBe('42')
    expect(formatLiveValue(true)).toBe('1')
    expect(formatLiveValue([1, 2, 3, 4])).toBe('[1 2 3 …]')
    expect(formatLiveValue(undefined)).toBeUndefined()
    expect([truthy(0), truthy(2.5), truthy(false), truthy('x')]).toEqual([false, true, false, undefined])
  })
})

describe('block live state', () => {
  const plan = planVmUpload(project, vm, { maxFrameBytes: 240, debug: true })
  const layout = plan.program.objects
  const wire = (id: string) => layout.wireIdOf.get(id)!
  const pick = plan.debug!.blocks.get('pick')!
  const state = (tickValue: number | undefined, eno: number | undefined): LiveValues => new Map<number, Uint8Array>([
    [wire('table'), f32(10, 20, 30)],
    [wire('sel'), Uint8Array.of(1)],
    ...(tickValue === undefined ? [] : [[wire('every:eno'), Uint8Array.of(tickValue)] as [number, Uint8Array]]),
    ...(eno === undefined ? [] : [[wire('pick:eno'), Uint8Array.of(eno)] as [number, Uint8Array]]),
  ])

  it('follows the enable source and the ENO of the block', () => {
    const silent = blockLive(pick, layout, state(undefined, undefined))
    expect(silent.en).toBe('unknown')
    expect(silent.eno).toBeUndefined()
    expect(blockLive(pick, layout, state(1, 1))).toMatchObject({ en: 'open', eno: true })
    expect(blockLive(pick, layout, state(0, 0))).toMatchObject({ en: 'closed', eno: false })
  })

  it('holds a pulse: a gate or ENO true a moment ago still shows true', () => {
    const closedNow = state(0, 0)
    expect(blockLive(pick, layout, closedNow)).toMatchObject({ en: 'closed', eno: false })
    expect(blockLive(pick, layout, closedNow, (w) => w === wire('every:eno'))).toMatchObject({ en: 'open', eno: false })
    expect(blockLive(pick, layout, closedNow, (w) => w === wire('pick:eno'))).toMatchObject({ en: 'closed', eno: true })
    expect(blockLive(pick, layout, closedNow, () => true)).toMatchObject({ en: 'open', eno: true })
  })

  it('prints the pin values, following the dynamic position', () => {
    expect(blockLive(pick, layout, state(1, 1))).toMatchObject({ inputs: ['20'], enables: ['1'], outputs: [undefined] })
  })

  it('runs a block with no enable paths on every pass', () => {
    expect(blockLive(plan.debug!.blocks.get('every')!, layout, state(1, 1)).en).toBe('always')
  })
})

describe('live watching', () => {
  const plan = planVmUpload(project, vm, { maxFrameBytes: 240, debug: true })
  const wire = (id: string) => plan.program.objects.wireIdOf.get(id)!

  it('adds what the user chose to the debug subscription, sorted, without doubles', () => {
    const base = plan.subscribed.filter((w) => w !== wire('table'))
    const { wires, dropped } = withWatched(plan.program, base, ['table', 'sel', 'nothing'], 100)
    expect(wires).toContain(wire('table'))
    expect(wires.filter((w) => w === wire('sel'))).toHaveLength(1)
    expect(wires).toEqual([...wires].sort((a, b) => a - b))
    expect(dropped).toEqual([])
  })

  it('keeps the chosen ones that do not fit the packet apart', () => {
    const base = plan.subscribed.filter((w) => w !== wire('table'))
    const { wires, dropped } = withWatched(plan.program, base, ['table'], base.length)
    expect(wires).not.toContain(wire('table'))
    expect(dropped).toEqual(['table'])
  })

  it('knows how many objects a subscribe holds', () => {
    expect(subscribeRoom(vm, 240)).toBeGreaterThan(20)
    expect(subscribeRoom(vm, 4)).toBe(0)
  })

  it('counts blocks in block mode, wrapping after the last', () => {
    expect(nextBlockIndex(undefined, 3, 'enter')).toBe(0)
    expect(nextBlockIndex(0, 3, 'next')).toBe(1)
    expect(nextBlockIndex(2, 3, 'next')).toBe(0)
    expect(nextBlockIndex(0, 0, 'next')).toBeUndefined()
  })
})
