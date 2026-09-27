import { describe, expect, it } from 'vitest'
import { compileObjects, packSubscribe, packValueWrite } from '../compiler'
import { runitCommandCatalog, runitStreamCatalog, runitVmCatalog } from '../descriptors'
import { createProject, parseProject, serializeProject, setFolderChildren, updateObject } from '../project'
import type { ObjectNode, ObjectSection, ProjectDocument, ValueNode } from '../project'
import { planSettingsUpload, planVmUpload, runitSettingsIds } from '.'
import type { BleCharSpec, BleServiceSpec, ConnectorSpec, SettingsState } from '.'

const hex = (data: Uint8Array): string => [...data].map((byte) => byte.toString(16).padStart(2, '0')).join(' ')
const vm = runitVmCatalog()
const value = (id: string, name: string, extra: Partial<ValueNode> = {}): ValueNode => ({ kind: 'value', id, name, type: 'U8', length: 1, mutable: true, retentive: false, ...extra })
const project = (objects: readonly ObjectNode[]): ProjectDocument => ({ ...createProject('test'), objects })
const headFlags = (record: Uint8Array): number => record[2 + 3]!

describe('object tree data', () => {
  it('keeps subscriptions through save and load', () => {
    const doc = project([{ kind: 'folder', id: 'f', name: 'f', subscribed: true, children: [value('a', 'a', { subscribed: false })] }])
    expect(parseProject(serializeProject(doc))).toEqual(doc)
  })

  it('lets a folder be subscribed', () => {
    const doc = updateObject(project([{ kind: 'folder', id: 'f', name: 'f', children: [] }]), 'f', { subscribed: true })
    expect(doc.objects[0]).toMatchObject({ subscribed: true })
  })

  it('refuses folder children that reuse an ID from elsewhere', () => {
    const doc = project([{ kind: 'folder', id: 'f', name: 'f', children: [value('row', '')] }, value('a', 'a')])
    expect(() => setFolderChildren(doc, 'f', [value('a', '')])).toThrow(/already in the project/)
    expect(setFolderChildren(doc, 'f', [value('row', '', { length: 3 }), value('row2', '')]).objects[0]).toMatchObject({ children: [{ id: 'row', length: 3 }, { id: 'row2' }] })
  })
})

describe('object compiler', () => {
  it('uploads folders as mutable, so the device accepts their links', () => {
    const compiled = compileObjects(project([{ kind: 'folder', id: 'f', name: 'f', children: [value('a', 'a', { mutable: false })] }]), vm)
    const mutableBit = 1 << (vm.head.fields.get('f.mutable')!.bit ?? 0)
    expect(headFlags(compiled.createRecords[0]!) & mutableBit).toBe(mutableBit)
    expect(headFlags(compiled.createRecords[1]!) & mutableBit).toBe(0)
  })

  it('rejects a retentive value without a name', () => {
    const compiled = compileObjects(project([{ kind: 'folder', id: 'f', name: 'f', children: [value('a', '', { retentive: true })] }]), vm)
    expect(compiled.diagnostics).toEqual([expect.objectContaining({ objectId: 'a', firmwareError: 'ERR_VM_RETAIN_UNNAMED' })])
  })

  it('pads a shorter text write with zeros', () => {
    const node = value('s', 's', { type: 'STR', length: 6, value: 'hello' })
    const { layout } = compileObjects(project([node]), vm)
    // 04 43 count | id 0, start 0, 6 bytes | 'hi' + 4 zeros
    expect(packValueWrite(vm, layout, node, 'hi', 64).map(hex)).toEqual(['04 43 01 00 00 00 00 06 00 68 69 00 00 00 00'])
  })
})

describe('object sections', () => {
  const blocks: ObjectSection = {
    key: 'blocks',
    owner: 'block',
    objects: [value('b-out', 'out', { type: 'F' }), { kind: 'folder', id: 'b-f', name: 'pid', children: [value('b-eno', 'eno', { type: 'B' })] }],
  }
  const user = project([value('u-a', 'a'), { kind: 'folder', id: 'u-f', name: 'view', children: [{ kind: 'reference', id: 'u-r', name: 'out', targetId: 'b-out' }] }])

  it('numbers block objects after the user objects, in one ID space', () => {
    const { layout, diagnostics } = compileObjects(user, vm, [blocks])
    expect(diagnostics).toEqual([])
    expect(layout.objects.map((entry) => [entry.wireId, entry.section, entry.node.id])).toEqual([
      [0, 'user', 'u-a'], [1, 'user', 'u-f'], [2, 'blocks', 'b-out'], [3, 'blocks', 'b-f'], [4, 'blocks', 'b-eno'],
    ])
    expect(layout.sections).toEqual([{ key: 'user', first: 0, count: 2 }, { key: 'blocks', first: 2, count: 3 }])
    // A user folder may link a block output.
    expect(layout.objects[1]!.children).toEqual([2])
    expect(layout.wireIdOf.get('u-r')).toBe(2)
  })

  it('marks block-owned objects resettable, user objects not', () => {
    const { createRecords } = compileObjects(user, vm, [blocks])
    const resettable = 1 << (vm.head.fields.get('f.upd_resetable')!.bit ?? 0)
    expect(createRecords.map((record) => (headFlags(record) & resettable) !== 0)).toEqual([false, false, true, true, true])
  })

  it('reports an ID used in two sections', () => {
    const clash: ObjectSection = { key: 'blocks', owner: 'block', objects: [value('u-a', 'x')] }
    expect(compileObjects(user, vm, [clash]).diagnostics).toEqual(expect.arrayContaining([expect.objectContaining({ objectId: 'u-a', message: expect.stringMatching(/used twice/) })]))
  })
})

describe('VM upload plan', () => {
  it('ends with a subscribe to the marked objects, skipping those a subscribed folder brings', () => {
    const doc = project([
      value('a', 'a', { subscribed: true }),
      { kind: 'folder', id: 'f', name: 'f', subscribed: true, children: [value('b', 'b', { subscribed: true })] },
      value('c', 'c'),
    ])
    const plan = planVmUpload(doc, vm, { maxFrameBytes: 64 })
    expect(plan.ok).toBe(true)
    expect(plan.subscribed).toEqual([0, 1])
    expect(plan.steps.map((step) => step.label)).toEqual(['vm open', 'vm add objects', 'vm set values', 'vm subscribe (2)'])
    expect(hex(plan.steps.at(-1)!.frame)).toBe('04 47 02 00 00 01 00')
  })

  it('sends no steps while the project has errors', () => {
    const plan = planVmUpload(project([value('a', '')]), vm, { maxFrameBytes: 64 })
    expect(plan).toMatchObject({ ok: false, steps: [] })
  })
})

describe('settings upload plan', () => {
  const commands = runitCommandCatalog()
  const layout = runitStreamCatalog().ble
  const ids = runitSettingsIds()
  const char = (id: string, uuid: string, extra: Partial<BleCharSpec> = {}): BleCharSpec => ({ id, name: id, uuid, read: true, write: false, writeNoResponse: false, notify: true, indicate: false, txBufferSize: 256, rxBufferSize: 0, ...extra })
  const system: BleServiceSpec = {
    id: 'svc-runit', name: 'runIT', uuid: '0xFFE0', system: true, isPrimary: true,
    characteristics: [char('chr-tx', '0xFFE1', { system: true }), char('chr-rx', '0xFFE2', { system: true, write: true, notify: false }), char('chr-logs', '0xFFE3', { system: true })],
  }
  const iface: ConnectorSpec = {
    id: ids.interfaceConnector, key: 'interface', name: 'interface', header: '0x05', system: true, maxPacketLen: 512, isSuspended: false,
    bindings: [{ id: 'tx', provider: 'BLE', endpoint: '0xFFE1', direction: 'TX' }, { id: 'rx', provider: 'BLE', endpoint: '0xFFE2', direction: 'RX' }],
  }
  const base: SettingsState = { services: [system], connectors: [iface] }
  const plan = (to: SettingsState, from = base) => planSettingsUpload(commands, layout, ids, from, to)
  const steps = (to: SettingsState, from = base) => plan(to, from).steps.map((step) => `${step.label}: ${hex(step.frame)}`)

  it('sends nothing when nothing changed', () => {
    expect(plan(base)).toEqual({ ok: true, steps: [], diagnostics: [] })
  })

  it('creates a new service with its characteristics', () => {
    const svc: BleServiceSpec = { id: 's', name: 's', uuid: '0xFF10', isPrimary: true, characteristics: [char('c', '0xFF11', { txBufferSize: 64 })] }
    expect(steps({ ...base, services: [system, svc] })).toEqual([
      'ble create service 0xFF10: 02 01 10 ff 01',
      // svc, uuid, write 0, indicate 0, notify 1, tx 64, rx 0, "c\0"
      'ble create char 0xFF11: 02 03 10 ff 11 ff 00 00 01 40 00 00 00 00 00 00 00 63 00',
      // the board stages GATT changes; apply puts them in the table
      'ble apply: 02 05',
    ])
  })

  it('replaces a changed characteristic and drops a removed service in one command', () => {
    const before: SettingsState = {
      ...base,
      services: [system, { id: 's', name: 's', uuid: '0xFF10', isPrimary: true, characteristics: [char('c', '0xFF11')] }, { id: 't', name: 't', uuid: '0xFF20', isPrimary: true, characteristics: [char('d', '0xFF21')] }],
    }
    const after: SettingsState = { ...base, services: [system, { id: 's', name: 's', uuid: '0xFF10', isPrimary: true, characteristics: [char('c', '0xFF11', { txBufferSize: 512 })] }] }
    expect(plan(after, before).steps.map((step) => step.label)).toEqual(['ble remove char 0xFF11', 'ble remove service 0xFF20', 'ble create char 0xFF11', 'ble apply'])
  })

  it('refuses to remove a system characteristic or cut the command link', () => {
    const noLogs: SettingsState = { ...base, services: [{ ...system, characteristics: system.characteristics.slice(0, 2) }] }
    expect(plan(noLogs)).toMatchObject({ ok: false, steps: [], diagnostics: [expect.objectContaining({ subjectId: 'chr-logs' })] })
    const unbound: SettingsState = { ...base, connectors: [{ ...iface, bindings: iface.bindings.slice(0, 1) }] }
    expect(plan(unbound).ok).toBe(false)
  })

  it('rejects two characteristics with one UUID, even in different services', () => {
    const svc: BleServiceSpec = { id: 's', name: 's', uuid: '0xFF10', isPrimary: true, characteristics: [char('c', '0xFFE1')] }
    expect(plan({ ...base, services: [system, svc] }).ok).toBe(false)
  })

  it('creates a user connector and binds it after the BLE changes, unbinds before; apply comes last', () => {
    const svc: BleServiceSpec = { id: 's', name: 's', uuid: '0xFF10', isPrimary: true, characteristics: [char('c', '0xFF11')] }
    const user: ConnectorSpec = {
      id: ids.appConnectorBase, key: 'app', name: 'app', header: '0x10', system: false, maxPacketLen: 128, isSuspended: false,
      bindings: [{ id: 'b', provider: 'BLE', endpoint: '0xFF11', direction: 'TX' }],
    }
    const added = { services: [system, svc], connectors: [iface, user] }
    expect(plan(added).steps.map((step) => step.label)).toEqual([
      'ble create service 0xFF10', 'ble create char 0xFF11', 'connector create app', 'connector app bind TX BLE 0xFF11', 'ble apply',
    ])
    expect(plan(base, added).steps.map((step) => step.label)).toEqual(['connector remove app', 'ble remove service 0xFF10', 'ble apply'])
  })
})

describe('limits from the firmware descriptors', () => {
  it('limits a subscribe to CONFIG_VM_SUB_MAX_SUBSCRIBERS, not the add-objects batch', () => {
    expect(vm.wire.subscribe.batch).toEqual({ max: 128, countBytes: 1 })
    expect(vm.wire.addObjects.batch).toEqual({ max: 255, countBytes: 1 })
    expect(vm.wire.setData.recordSize).toBe(6)
    const ids = (count: number) => Array.from({ length: count }, (_, index) => index)
    expect(packSubscribe(vm, ids(128), 600).byteLength).toBe(3 + 128 * 2)
    expect(() => packSubscribe(vm, ids(129), 600)).toThrow(/at most 128/)
  })

  it('sizes objects by the payload_size type', () => {
    expect(vm.payloadMax).toBe(0xffff)
    expect(vm.maxElements(vm.type('F')!)).toBe(16383)
    expect(vm.maxElements(vm.type('U8')!)).toBe(65535)
    expect(compileObjects(project([value('big', 'big', { type: 'U32', length: 16384 })]), vm).diagnostics).toEqual([expect.objectContaining({ message: expect.stringMatching(/at most 65535 \(16383 elements/) })])
  })

  it('publishes the board setup: GATT profile, default bindings, registry limits', () => {
    const { board } = runitStreamCatalog()
    expect(board.service).toEqual({ uuid: 0xffe0, symbol: 'SYS_BLE_SVC_RUNIT', isPrimary: true })
    // RX: SYS_BUFF_SIZE_FOR(FRAME_MAX 512, 2) = (512 + 8) x 2
    expect(board.characteristics.find((char) => char.uuid === 0xffe2)).toMatchObject({ write: true, notify: false, rxBufferSize: 1040, txBufferSize: 0 })
    expect(board.bindings).toEqual(expect.arrayContaining([expect.objectContaining({ connectorId: 3, direction: 'rx', providerId: 1, endpoint: 0xffe2 })]))
    expect(board.limits).toMatchObject({ connectorsMax: 8, frameMax: 512 })
    expect(runitStreamCatalog().streams.every((stream) => stream.maxFrame === 512)).toBe(true)
  })

  it('checks connectors against the registry limits', () => {
    const ids = runitSettingsIds()
    const user = (extra: Partial<ConnectorSpec>): ConnectorSpec => ({ id: ids.appConnectorBase, key: 'u', name: 'u', header: '0x10', system: false, maxPacketLen: 64, isSuspended: false, bindings: [], ...extra })
    const iface: ConnectorSpec = {
      id: ids.interfaceConnector, key: 'interface', name: 'interface', header: '0x05', system: true, maxPacketLen: 512, isSuspended: false,
      bindings: [{ id: 'tx', provider: 'BLE', endpoint: '0xFFE1', direction: 'TX' }, { id: 'rx', provider: 'BLE', endpoint: '0xFFE2', direction: 'RX' }],
    }
    const planFor = (connector: ConnectorSpec) => planSettingsUpload(runitCommandCatalog(), runitStreamCatalog().ble, ids, { services: [], connectors: [iface] }, { services: [], connectors: [iface, connector] })
    expect(planFor(user({ id: ids.limits.connectorsMax })).ok).toBe(false)
    expect(planFor(user({ maxPacketLen: ids.limits.frameMax + 1 })).ok).toBe(false)
    expect(planFor(user({})).ok).toBe(true)
  })
})
