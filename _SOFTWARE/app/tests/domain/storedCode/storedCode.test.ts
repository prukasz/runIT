import { describe, expect, it } from 'vitest'
import { compileProgram } from '../../../src/domain/compiler'
import { runitCommandCatalog, runitDeviceCatalog, runitErrorCatalog, runitStreamCatalog, runitVmCatalog } from '../../../src/domain/descriptors'
import { createProject } from '../../../src/domain/project'
import type { ObjectNode, ObjectSection, ProjectDocument, ValueNode } from '../../../src/domain/project'
import { boardDefaultSettings, planSettingsUpload, runitSettingsIds } from '../../../src/domain/upload'
import type { BleCharSpec, SettingsState } from '../../../src/domain/upload'
import { buildStoredCode, crc32, decodeFrameList, decodeStoredCode, encodeFrameList, parseStoredCode, serializeStoredCode, storedFrameMax } from '../../../src/domain/storedCode'
import type { StoredCodeContext } from '../../../src/domain/storedCode'

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join(' ')
const ctx: StoredCodeContext = { vm: runitVmCatalog(), commands: runitCommandCatalog(), layout: runitStreamCatalog().ble, ids: runitSettingsIds(), devices: runitDeviceCatalog() }
const defaults = boardDefaultSettings()
const value = (id: string, name: string, type: string, extra: Partial<ValueNode> = {}): ValueNode => ({ kind: 'value', id, name, type, length: 1, mutable: true, retentive: false, ...extra })
const project = (objects: readonly ObjectNode[]): ProjectDocument => ({ ...createProject('test'), objects })

const char = (id: string, uuid: string, extra: Partial<BleCharSpec> = {}): BleCharSpec => ({ id, name: id, uuid, read: false, write: false, writeNoResponse: false, notify: true, indicate: false, txBufferSize: 256, rxBufferSize: 0, ...extra })

/** A project using every decoded feature: folders, references, each type, flags, values, subscriptions, block objects. */
const sample = project([
  { kind: 'folder', id: 'f-motor', name: 'motor', subscribed: true, children: [
    value('v-speed', 'speed', 'F', { value: [1.5, -2.25] , length: 3 }),
    value('v-en', 'enabled', 'B', { value: [true] }),
    { kind: 'folder', id: 'f-row', name: '', children: [value('v-row', '', 'I32', { length: 4, value: [-7, 0, 9] })] },
  ] },
  value('v-count', 'count', 'U32', { retentive: true, value: [42] }),
  value('v-label', 'label', 'STR', { length: 12, value: 'hello', mutable: false }),
  value('v-raw', 'raw', 'U8', { length: 2, subscribed: true }),
  { kind: 'folder', id: 'f-view', name: 'view', children: [{ kind: 'reference', id: 'r-count', name: 'count', targetId: 'v-count' }, { kind: 'reference', id: 'r-out', name: 'out', targetId: 'b-out' }] },
])
const blocks: ObjectSection = { key: 'blocks', owner: 'block', objects: [value('b-out', 'out', 'F'), value('b-eno', 'eno', 'B')] }

const userSettings: SettingsState = {
  logs: defaults.logs,
  services: [
    defaults.services[0]!,
    { id: 's-user', name: 's', uuid: '0xFF10', isPrimary: true, characteristics: [char('c-a', '0xFF11', { txBufferSize: 64 }), char('c-extra', '0xFF30'), char('c-b', '0xFF12', { write: true, writeNoResponse: true, notify: false, txBufferSize: 0, rxBufferSize: 520 })] },
  ],
  connectors: [
    ...defaults.connectors,
    { id: ctx.ids.appConnectorBase, key: 'app', name: 'app', header: '0x10', system: false, maxPacketLen: 128, isSuspended: true, bindings: [{ id: 'b1', provider: 'BLE', endpoint: '0xFF11', direction: 'TX' }, { id: 'b2', provider: 'UART', endpoint: '0', direction: 'TX_RX' }] },
  ],
}

const build = () => buildStoredCode({ project: sample, sections: [blocks], settings: userSettings, boardDefaults: defaults }, ctx)

describe('stored code: frame list', () => {
  it('is length-prefixed frames, round-trips, and has the zlib CRC-32', () => {
    const frames = [Uint8Array.from([0x02, 0x01, 0x10, 0xff, 0x01]), Uint8Array.from([0x04, 0x40])]
    const bytes = encodeFrameList(frames)
    expect(hex(bytes)).toBe('05 00 02 01 10 ff 01 02 00 04 40')
    expect(decodeFrameList(bytes).map(hex)).toEqual(frames.map(hex))
    expect(crc32(new TextEncoder().encode('123456789'))).toBe(0xcbf43926)
    expect(() => decodeFrameList(bytes.subarray(0, 6))).toThrow(/claims 5 bytes/)
  })

  it('exports and imports with a CRC check', () => {
    const code = { schemaId: runitErrorCatalog().schemaId, steps: build().steps }
    const parsed = parseStoredCode(serializeStoredCode(code))
    expect(parsed.schemaId).toBe(code.schemaId)
    expect(parsed.steps.map((step) => hex(step.frame))).toEqual(code.steps.map((step) => hex(step.frame)))
    expect(() => parseStoredCode(serializeStoredCode(code).replace(/"crc32": "0x[0-9A-F]+"/, '"crc32": "0x00000000"'))).toThrow(/crc32/)
  })
})

describe('stored code: build', () => {
  it('orders settings before the VM program, sized for the replay path, with no exec', () => {
    const code = build()
    expect(code.diagnostics.filter((entry) => entry.severity === 'error')).toEqual([])
    expect(code.ok).toBe(true)
    const classes = code.steps.map((step) => step.frame[0])
    expect(classes.lastIndexOf(0x02)).toBeLessThan(classes.indexOf(0x04))
    expect(classes.lastIndexOf(0x06)).toBeLessThan(classes.indexOf(0x04))
    expect(code.steps.at(-1)!.label).toMatch(/^vm subscribe/)
    expect(code.steps.every((step) => step.frame.byteLength <= storedFrameMax(ctx.ids))).toBe(true)
    expect(code.steps.some((step) => step.frame[0] === ctx.vm.classHeader && step.frame[1] === ctx.vm.packets.exec)).toBe(false)
  })

  it('refuses an exec frame among the extra frames', () => {
    const exec = { label: 'run', frame: Uint8Array.from([ctx.vm.classHeader, ctx.vm.packets.exec, 5]) }
    expect(buildStoredCode({ project: sample, settings: userSettings, boardDefaults: defaults, extraFrames: [exec] }, ctx).ok).toBe(false)
  })
})

describe('stored code: recovery keeps the data', () => {
  const code = build()
  const recovered = decodeStoredCode(code.steps.map((step) => step.frame), defaults, ctx)

  it('decodes without errors', () => {
    expect(recovered.diagnostics).toEqual([])
    expect(recovered.extraFrames).toEqual([])
  })

  it('settings: re-applying the recovered settings needs no command', () => {
    const plan = planSettingsUpload(ctx.commands, ctx.layout, ctx.ids, userSettings, recovered.settings)
    expect(plan.diagnostics.filter((entry) => entry.severity === 'error')).toEqual([])
    expect(plan.steps).toEqual([])
    expect(recovered.settings.connectors.find((connector) => connector.id === ctx.ids.appConnectorBase)).toMatchObject({ name: 'app', isSuspended: true, maxPacketLen: 128 })
  })

  it('VM: the recovered program compiles to the same frames', () => {
    const maxFrameBytes = storedFrameMax(ctx.ids)
    const original = compileProgram(sample, ctx.vm, { maxFrameBytes, sections: [blocks] })
    const again = compileProgram(recovered.project, ctx.vm, { maxFrameBytes, sections: recovered.sections })
    expect(again.diagnostics).toEqual([])
    expect(again.frames.map(hex)).toEqual(original.frames.map(hex))
  })

  it('VM: names, types, values, flags, folders, references, subscriptions and block objects come back', () => {
    const [motor, count, label, raw, view] = recovered.project.objects
    expect(motor).toMatchObject({ kind: 'folder', name: 'motor', subscribed: true })
    expect(motor?.kind === 'folder' && motor.children.map((child) => child.name)).toEqual(['speed', 'enabled', ''])
    expect(count).toMatchObject({ kind: 'value', type: 'U32', retentive: true, value: [42] })
    expect(label).toMatchObject({ kind: 'value', type: 'STR', length: 12, value: 'hello', mutable: false })
    expect(raw).toMatchObject({ kind: 'value', type: 'U8', length: 2, subscribed: true })
    expect(view?.kind === 'folder' && view.children.map((child) => child.kind)).toEqual(['reference', 'reference'])
    expect(recovered.sections).toEqual([expect.objectContaining({ key: 'blocks', owner: 'block', objects: [expect.objectContaining({ name: 'out' }), expect.objectContaining({ name: 'eno' })] })])
  })

  it('keeps frames of classes without a decoder, in order', () => {
    const power = runitCommandCatalog().commands.find((command) => command.group.id === 'power')!
    const frame = Uint8Array.from([power.classHeader, power.packetHeader, 1, 2, 3])
    const withExtra = decodeStoredCode([frame, ...code.steps.map((step) => step.frame)], defaults, ctx)
    expect(withExtra.extraFrames.map((step) => hex(step.frame))).toEqual([hex(frame)])
  })
})
