import { describe, expect, it } from 'vitest'
import { runitVmCatalog } from '../../../src/domain/descriptors'
import { createProject } from '../../../src/domain/project'
import type { ObjectNode, ProjectDocument, ValueNode } from '../../../src/domain/project'
import { compileProgram } from '../../../src/domain/compiler'

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join(' ')
const catalog = runitVmCatalog()
const value = (id: string, name: string, type: string, extra: Partial<ValueNode> = {}): ValueNode => ({ kind: 'value', id, name, type, length: 1, mutable: true, retentive: false, ...extra })
const project = (objects: readonly ObjectNode[]): ProjectDocument => ({ ...createProject('test'), objects })

const sample = project([
  { kind: 'folder', id: 'f-motor', name: 'motor', children: [value('v-speed', 'speed', 'F', { value: [1.5] }), value('v-en', 'enabled', 'B', { value: [true] })] },
  value('v-count', 'count', 'U32', { retentive: true }),
])

describe('VM catalog', () => {
  it('reads packet bytes, types and header layout from the descriptors', () => {
    expect(catalog.classHeader).toBe(0x04)
    expect(catalog.packets).toMatchObject({ open: 0x41, addObjects: 0x42, setData: 0x43 })
    expect(catalog.types.map((type) => type.key)).toEqual(['PTR', 'U8', 'U32', 'I32', 'F', 'B', 'STR'])
    expect(catalog.head.size).toBe(4)
    expect(catalog.nameMax).toBe(15)
  })
})

describe('compileProgram: objects', () => {
  it('numbers objects in tree order, a folder before its children', () => {
    const compiled = compileProgram(sample, catalog, { maxFrameBytes: 64 })
    expect(compiled.diagnostics).toEqual([])
    expect([...compiled.objects.wireIdOf]).toEqual([['f-motor', 0], ['v-speed', 1], ['v-en', 2], ['v-count', 3]])
    expect(compiled.objects.objects[0]?.children).toEqual([1, 2])
  })

  it('uses the target wire ID for a reference without creating another object', () => {
    const linked = project([
      value('v', 'speed', 'F'),
      { kind: 'folder', id: 'f', name: 'links', children: [{ kind: 'reference', id: 'r', name: 'speed', targetId: 'v' }] },
    ])
    const compiled = compileProgram(linked, catalog, { maxFrameBytes: 64 })
    expect(compiled.ok).toBe(true)
    expect(compiled.counts.objects).toBe(2)
    expect(compiled.objects.wireIdOf.get('r')).toBe(compiled.objects.wireIdOf.get('v'))
    expect(compiled.objects.objects[1]?.children).toEqual([0])
  })

  it('rejects reference cycles between folders', () => {
    const cyclic = project([
      { kind: 'folder', id: 'a', name: 'a', children: [{ kind: 'reference', id: 'ra', name: 'b', targetId: 'b' }] },
      { kind: 'folder', id: 'b', name: 'b', children: [{ kind: 'reference', id: 'rb', name: 'a', targetId: 'a' }] },
    ])
    const compiled = compileProgram(cyclic, catalog, { maxFrameBytes: 64 })
    expect(compiled.ok).toBe(false)
    expect(compiled.diagnostics).toEqual(expect.arrayContaining([expect.objectContaining({ severity: 'error', message: expect.stringContaining('cycle') })]))
  })

  // head: u16 payload_size, obj_t | name_size << 4, flags (mutable bit 0, tagged bit 3, retentive bit 4)
  it('builds the open, add-objects and set-values frames', () => {
    const compiled = compileProgram(sample, catalog, { maxFrameBytes: 64 })
    expect(compiled.ok).toBe(true)
    // arena: registry 4 x 4 + motor 20 + speed 16 + enabled 12 + count 16
    expect(compiled.arenaBytes).toBe(80)
    expect(compiled.retainBytes).toBe(2 + 4 + 5 + 4)
    expect(compiled.frames.map(hex)).toEqual([
      '04 41 04 00 00 00 00 00 50 00 00 00',
      [
        '04 42 04',
        '00 00 08 00 51 09 6d 6f 74 6f 72', // motor: PTR x 2 (8 B), mutable: links are 0x43 writes
        '01 00 04 00 55 09 73 70 65 65 64', // speed: F, mutable
        '02 00 01 00 76 09 65 6e 61 62 6c 65 64', // enabled: B
        '03 00 04 00 53 19 63 6f 75 6e 74', // count: U32, mutable + retentive
      ].join(' '),
      [
        '04 43 03',
        '01 00 00 00 04 00 00 00 c0 3f', // speed = 1.5
        '02 00 00 00 01 00 01', // enabled = true
        '00 00 00 00 04 00 01 00 02 00', // motor -> [speed, enabled]
      ].join(' '),
    ])
  })

  it('splits batches and long values to the frame size, by whole elements', () => {
    const long = project([value('v-a', 'a', 'U8', { length: 40, value: Array.from({ length: 40 }, (_, index) => index + 1) }), value('v-b', 'b', 'U8', { value: [7] })])
    const compiled = compileProgram(long, catalog, { maxFrameBytes: 20 })
    expect(compiled.ok).toBe(true)
    expect(compiled.frames.every((frame) => frame.byteLength <= 20)).toBe(true)
    const data = compiled.frames.filter((frame) => frame[1] === 0x43)
    // 11 bytes per record (20 - 3 - 6): a in 11 + 11 + 11 + 7; b (7 B) no longer fits beside the last chunk (16 B)
    expect(data.map((frame) => [frame[2], new DataView(frame.buffer).getUint16(3, true), new DataView(frame.buffer).getUint16(5, true)])).toEqual([[1, 0, 0], [1, 0, 11], [1, 0, 22], [1, 0, 33], [1, 1, 0]])
    expect(compiled.frames.filter((frame) => frame[1] === 0x42)).toHaveLength(1)
  })

  it('leaves out empty folders and zero values', () => {
    const compiled = compileProgram(project([{ kind: 'folder', id: 'f', name: 'empty', children: [] }, value('v', 'x', 'I32', { value: [0] })]), catalog, { maxFrameBytes: 64 })
    expect(compiled.ok).toBe(true)
    expect(compiled.counts.objects).toBe(1)
    expect(compiled.diagnostics).toEqual([expect.objectContaining({ severity: 'warning', objectId: 'f' })])
    expect(compiled.frames.map((frame) => frame[1])).toEqual([0x41, 0x42])
  })

  it('refuses what the firmware would refuse, before upload', () => {
    const bad = project([
      value('a', 'a_name_that_is_too_long', 'U8'),
      value('b', 'dup', 'U8'),
      value('c', 'dup', 'U8'),
      value('d', 'kind', 'U16'),
      value('e', 'range', 'U8', { value: [256] }),
      value('f', 'flag', 'B', { value: [1] }),
      value('g', 'text', 'STR', { length: 3, value: 'long' }),
      value('h', 'none', 'U8', { length: 0 }),
      { kind: 'folder', id: 'i', name: 'x', children: [value('j', 'keep', 'U8', { retentive: true })] },
      value('k', 'keep', 'U8', { retentive: true }),
    ])
    const compiled = compileProgram(bad, catalog, { maxFrameBytes: 64 })
    expect(compiled.ok).toBe(false)
    expect(compiled.frames).toEqual([])
    const byObject = Object.fromEntries(compiled.diagnostics.map((entry) => [entry.objectId, entry.firmwareError ?? entry.message]))
    expect(byObject).toMatchObject({ a: 'ERR_VM_OBJ_NAME_TOO_LONG', d: 'ERR_VM_OBJ_BAD_TYPE', e: 'ERR_VM_LOAD_DATA_RANGE', f: 'ERR_VM_LOAD_DATA_RANGE', g: 'ERR_VM_LOAD_DATA_RANGE', h: 'ERR_VM_OBJ_EMPTY' })
    expect(byObject.c).toMatch(/Two objects named 'dup'/)
    expect(byObject.k).toMatch(/Two retentive objects named 'keep'/)
  })

  it('checks the retain budget and the arena size', () => {
    const retained = project([value('r', 'big', 'U8', { length: catalog.retainMaxBytes, retentive: true })])
    expect(compileProgram(retained, catalog, { maxFrameBytes: 64 }).diagnostics.map((entry) => entry.firmwareError)).toEqual(['ERR_VM_RETAIN_TOO_BIG'])
    const huge = project([0, 1, 2].map((index) => value(`h${index}`, `huge${index}`, 'U8', { length: catalog.arena.maxBytes / 3 })))
    expect(compileProgram(huge, catalog, { maxFrameBytes: 64 }).diagnostics.map((entry) => entry.firmwareError)).toEqual(['ERR_VM_LOAD_TOO_BIG'])
  })

  it('rejects floating references at the top level', () => {
    const floating = project([
      value('v1', 'speed', 'U8'),
      { kind: 'reference', id: 'r1', name: 'speed_alias', targetId: 'v1' },
    ])
    const compiled = compileProgram(floating, catalog, { maxFrameBytes: 64 })
    expect(compiled.ok).toBe(false)
    expect(compiled.diagnostics).toEqual(
      expect.arrayContaining([
        expect.objectContaining({
          objectId: 'r1',
          severity: 'error',
          message: expect.stringMatching(/cannot exist at the top level/),
        }),
      ]),
    )
  })
})
