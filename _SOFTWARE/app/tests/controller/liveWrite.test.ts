import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest'
import { createWriter } from '../../src/controller/liveWrite'
import { controllerVariables, numberOfLive } from '../../src/controller/variables'
import { createProject } from '../../src/domain/project'

describe('createWriter', () => {
  beforeEach(() => vi.useFakeTimers())
  afterEach(() => vi.useRealTimers())

  it('sends the first value at once, then only the newest one per interval', async () => {
    const sent: number[] = []
    const write = createWriter(async (_id, value) => { sent.push(value) }, () => undefined, 50, () => Date.now())
    write('a', 1)
    expect(sent).toEqual([1])
    write('a', 2)
    write('a', 3)
    expect(sent).toEqual([1])
    await vi.advanceTimersByTimeAsync(60)
    expect(sent).toEqual([1, 3])
  })

  it('keeps variables apart and reports a failed write without stopping later ones', async () => {
    const sent: string[] = []
    const failures: unknown[] = []
    const write = createWriter(async (id, value) => { if (value < 0) throw new Error('no'); sent.push(`${id}=${value}`) }, (failure) => failures.push(failure), 10, () => Date.now())
    write('a', -1)
    write('b', 5)
    await vi.advanceTimersByTimeAsync(20)
    write('a', 7)
    await vi.advanceTimersByTimeAsync(20)
    expect(failures).toHaveLength(1)
    expect(sent).toEqual(['b=5', 'a=7'])
  })
})

describe('controller variables', () => {
  it('lists only single values, named with their folders, and reads live values as numbers', () => {
    const project = {
      ...createProject('t'),
      objects: [
        { kind: 'folder' as const, id: 'f', name: 'motors', children: [{ kind: 'value' as const, id: 'v1', name: 'left', type: 'F', length: 1, mutable: true, retentive: false }] },
        { kind: 'value' as const, id: 'v2', name: 'gains', type: 'F', length: 3, mutable: true, retentive: false },
        { kind: 'value' as const, id: 'v3', name: 'text', type: 'STR', length: 8, mutable: true, retentive: false },
      ],
    }
    expect(controllerVariables(project)).toEqual([{ id: 'v1', name: 'motors.left', type: 'F', mutable: true }])
    expect([numberOfLive(true), numberOfLive([4, 5]), numberOfLive('x'), numberOfLive(undefined), numberOfLive(2.5)]).toEqual([1, 4, undefined, undefined, 2.5])
  })
})
