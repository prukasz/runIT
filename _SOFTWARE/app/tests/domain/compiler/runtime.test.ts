import { describe, expect, it } from 'vitest'
import { applyVmValues, decodeVmElements, decodeVmTelemetry } from '../../../src/domain/decoder'
import { runitVmCatalog } from '../../../src/domain/descriptors'
import { createProject } from '../../../src/domain/project'
import type { ValueNode } from '../../../src/domain/project'
import { compileProgram, packExec, packSubscribe, packValueWrite } from '../../../src/domain/compiler'

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join(' ')
const catalog = runitVmCatalog()
const speed: ValueNode = { kind: 'value', id: 'v-speed', name: 'speed', type: 'F', length: 2, mutable: true, retentive: false }
const label: ValueNode = { kind: 'value', id: 'v-label', name: 'label', type: 'STR', length: 6, mutable: true, retentive: false }
const compiled = compileProgram({ ...createProject('t'), objects: [speed, label] }, catalog, { maxFrameBytes: 64 })

describe('runtime packets', () => {
  it('packs subscribe and execution control', () => {
    expect(hex(packSubscribe(catalog, [0, 1, 0x8001], 64))).toBe('04 47 03 00 00 01 00 01 80')
    expect(() => packSubscribe(catalog, Array.from({ length: 40 }, (_, index) => index), 64)).toThrow(/needs a 83-byte frame/)
    expect(hex(packExec(catalog, 5))).toBe('04 48 05')
  })

  it('writes a value at run time, zeros included', () => {
    expect(packValueWrite(catalog, compiled.objects, speed, [0, 2], 64).map(hex)).toEqual(['04 43 01 00 00 00 00 08 00 00 00 00 00 00 00 00 40'])
    expect(() => packValueWrite(catalog, compiled.objects, speed, [1, 2, 3], 64)).toThrow(/3 values for 2/)
    expect(() => packValueWrite(catalog, compiled.objects, { ...speed, id: 'other' }, [1], 64)).toThrow(/not in the uploaded program/)
  })
})

describe('VM telemetry', () => {
  it('decodes value records and merges partial ones', () => {
    // stream 0x02, class 04, packet 43: speed[1] = 1.5, then label = "hi"
    const frame = Uint8Array.from([0x02, 0x04, 0x43, 2, 0, 0, 1, 0, 4, 0, 0, 0, 0xc0, 0x3f, 1, 0, 0, 0, 2, 0, 0x68, 0x69])
    const decoded = decodeVmTelemetry(frame, catalog)
    expect(decoded?.kind).toBe('values')
    const records = decoded?.kind === 'values' ? decoded.records : []
    const sizes = new Map([[0, { bytes: 8, wireWidth: 4 }], [1, { bytes: 6, wireWidth: 1 }]])
    const values = applyVmValues(new Map(), records, (id) => sizes.get(id))
    expect(decodeVmElements(catalog.type('F')!, values.get(0)!)).toEqual([0, 1.5])
    expect(decodeVmElements(catalog.type('STR')!, values.get(1)!)).toBe('hi')
    expect(decodeVmTelemetry(Uint8Array.from([0x05, 0x04, 0x43, 0]), catalog)).toBeUndefined()
    expect(() => decodeVmTelemetry(Uint8Array.from([0x02, 0x04, 0x43, 1, 0, 0]), catalog)).toThrow(/runs past the frame/)
  })

  it('decodes heap object descriptions', () => {
    // id 0x8000, head: 4 bytes payload, F | name_size 2 << 4, flags 0, name "ab"
    const frame = Uint8Array.from([0x02, 0x04, 0x42, 1, 0x00, 0x80, 4, 0, 0x25, 0, 0x61, 0x62])
    expect(decodeVmTelemetry(frame, catalog)).toMatchObject({ kind: 'describe', objects: [{ id: 0x8000, name: 'ab' }] })
  })
})
