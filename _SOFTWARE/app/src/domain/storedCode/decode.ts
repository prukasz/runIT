import { unpackStruct } from '../../backend/packetPack'
import type { PacketStructValues } from '../../backend/packetPack'
import { decodeVmElements } from '../decoder'
import { decodeVmObjectHead } from '../descriptors'
import type { CommandDescriptor, VmObjectType } from '../descriptors'
import { createProject } from '../project'
import { boardDeviceRef } from '../project'
import type { ActionStep, FolderNode, ObjectNode, ObjectSection, ProgramBlock, ProjectDevice, ProjectDocument, ValueNode } from '../project'
import type { BleCharSpec, BleServiceSpec, ConnectorBindingSpec, ConnectorSpec, LogSpec, SettingsState, UploadDiagnostic } from '../upload'
import { arrangeProgram, placeBlocks } from '../canvas'
import type { StoredCodeContext } from './build'
import { recoverBlocks } from './decodeBlocks'
import type { RawAccessor, RawBlock, RawStep } from './decodeBlocks'
import type { StoredCodeStep } from './frameList'

/*
 * Stored code → data (recovery, app/docs/03_records_as_storage.md §5). The
 * frames are applied in memory the way the board applies them at boot:
 * settings onto the board defaults, the VM program from its open. What the
 * frames don't carry (project IDs, descriptions, auto-type) is generated or
 * left out; everything the board holds comes back.
 *
 * Blocks come back from the accessor and block frames (decodeBlocks.ts):
 * types, pins as paths, enables, settings, formulas, loops. Their names and
 * places aren't in the frames; IDs are `<type><n>`, places are laid out.
 *
 * Known ambiguity: a folder whose first child is a reference to the object
 * numbered right after the folder's subtree decodes as owning that object
 * (both trees give the same frames).
 */

export interface RecoveredCode {
  readonly project: ProjectDocument
  /** Block-owned objects (`upd_resetable` set) that no recovered block owns, as their own section. */
  readonly sections: readonly ObjectSection[]
  /** The program's blocks from the 0x44 / 0x45 frames, in execution order, without positions (domain/canvas/layout.ts places them). */
  readonly blocks: readonly ProgramBlock[]
  /** Board defaults with the stored settings applied. */
  readonly settings: SettingsState
  /** Devices from install frames (names and tags generated). */
  readonly devices: readonly ProjectDevice[]
  /** Contract calls on a known device (board or recovered): the default settings. */
  readonly setup: readonly ActionStep[]
  /** Frames of classes the app has no decoder for, in order; stored again unchanged. */
  readonly extraFrames: readonly StoredCodeStep[]
  readonly diagnostics: readonly UploadDiagnostic[]
}

const hex = (value: number, digits: number): string => `0x${value.toString(16).padStart(digits, '0').toUpperCase()}`
const num = (values: PacketStructValues, name: string): number => Number(values[name] ?? 0)
const text = (values: PacketStructValues, name: string): string => String(values[name] ?? '')

// ---------------------------------------------------------------------------
// Settings: BLE (0x02) and data connectors (0x06)
// ---------------------------------------------------------------------------

type Direction = 'TX' | 'RX'

interface MutableConnector {
  spec: ConnectorSpec
  /** `TX:<provider id>` → endpoint; the board keeps one per provider and direction. */
  bindings: Map<string, number>
  /** Endpoint texts of the defaults, kept when a binding doesn't change. */
  texts: Map<string, string>
}

class SettingsReplay {
  readonly services: { spec: BleServiceSpec; chars: BleCharSpec[] }[]
  readonly connectors: MutableConnector[]
  private logs: LogSpec
  private readonly providerName: (id: number) => 'BLE' | 'UART' | undefined
  private readonly diagnostics: UploadDiagnostic[]

  constructor(defaults: SettingsState, ctx: StoredCodeContext, diagnostics: UploadDiagnostic[]) {
    this.diagnostics = diagnostics
    this.logs = defaults.logs
    this.providerName = (id) => (id === ctx.ids.providers.BLE ? 'BLE' : id === ctx.ids.providers.UART ? 'UART' : undefined)
    this.services = defaults.services.map((service) => ({ spec: service, chars: [...service.characteristics] }))
    this.connectors = defaults.connectors.map((connector) => {
      const bindings = new Map<string, number>()
      const texts = new Map<string, string>()
      for (const binding of connector.bindings) {
        const endpoint = parseEndpoint(binding)
        if (endpoint === undefined) continue
        for (const direction of directionsOf(binding)) {
          const key = `${direction}:${ctx.ids.providers[binding.provider]}`
          bindings.set(key, endpoint)
          texts.set(`${key}:${endpoint}`, binding.endpoint)
        }
      }
      return { spec: connector, bindings, texts }
    })
  }

  private warn(message: string) {
    this.diagnostics.push({ severity: 'warning', message })
  }

  apply(command: CommandDescriptor, values: PacketStructValues): void {
    switch (command.id) {
      case 'packet_settings_ble_service_create_t': {
        const uuid = num(values, 'uuid')
        if (this.serviceByUuid(uuid)) return this.warn(`Service ${hex(uuid, 4)} is created twice; the board refuses the second.`)
        this.services.push({ spec: { id: `svc-${hex(uuid, 4)}`, name: `Service ${hex(uuid, 4)}`, uuid: hex(uuid, 4), isPrimary: num(values, 'is_primary') !== 0, characteristics: [] }, chars: [] })
        return
      }
      case 'packet_settings_ble_service_remove_t': {
        const index = this.services.findIndex((entry) => uuidOf(entry.spec.uuid) === num(values, 'uuid'))
        if (index >= 0) this.services.splice(index, 1)
        return
      }
      case 'packet_settings_ble_char_create_t': {
        const service = this.serviceByUuid(num(values, 'service_uuid'))
        const uuid = num(values, 'uuid')
        if (!service) return this.warn(`Characteristic ${hex(uuid, 4)} names service ${hex(num(values, 'service_uuid'), 4)}, which doesn't exist.`)
        const isWrite = num(values, 'is_write') !== 0
        service.chars.push({
          id: `chr-${hex(uuid, 4)}`,
          name: text(values, 'name'),
          uuid: hex(uuid, 4),
          read: false,
          write: isWrite,
          writeNoResponse: isWrite,
          notify: num(values, 'is_notify') !== 0,
          indicate: num(values, 'is_indicate') !== 0,
          txBufferSize: num(values, 'tx_buffer_size'),
          rxBufferSize: num(values, 'rx_buffer_size'),
        })
        return
      }
      case 'packet_settings_ble_apply_t':
        return // applies the staged changes above: nothing of its own
      case 'packet_settings_ble_char_remove_t': {
        const service = this.serviceByUuid(num(values, 'service_uuid'))
        if (service) service.chars = service.chars.filter((char) => uuidOf(char.uuid) !== num(values, 'uuid'))
        return
      }
      case 'packet_settings_data_connector_create_t': {
        const id = num(values, 'id')
        const existing = this.connectors.find((entry) => entry.spec.id === id)
        const name = text(values, 'name')
        const config = { name, header: hex(num(values, 'header'), 2), maxPacketLen: num(values, 'max_packet_len') }
        if (existing) {
          if (existing.spec.system) return this.warn(`Connector ${id} is a system connector; the board refuses to change it.`)
          existing.spec = { ...existing.spec, ...config }
        } else {
          this.connectors.push({ spec: { id, key: name || `connector_${id}`, system: false, isSuspended: false, bindings: [], ...config }, bindings: new Map(), texts: new Map() })
        }
        return
      }
      case 'packet_settings_data_connector_remove_t': {
        const index = this.connectors.findIndex((entry) => entry.spec.id === num(values, 'id') && !entry.spec.system)
        if (index >= 0) this.connectors.splice(index, 1)
        return
      }
      case 'packet_settings_data_connector_tx_add_t':
      case 'packet_settings_data_connector_rx_add_t':
      case 'packet_settings_data_connector_tx_remove_t':
      case 'packet_settings_data_connector_rx_remove_t': {
        const connector = this.connectors.find((entry) => entry.spec.id === num(values, 'connector_id'))
        if (!connector) return this.warn(`A binding names connector ${num(values, 'connector_id')}, which doesn't exist.`)
        const key = `${command.id.includes('_tx_') ? 'TX' : 'RX'}:${num(values, 'provider_id')}`
        if (command.id.endsWith('_add_t')) connector.bindings.set(key, num(values, 'provider_param'))
        else connector.bindings.delete(key)
        return
      }
      case 'packet_settings_data_connector_suspend_t':
      case 'packet_settings_data_connector_resume_t': {
        const connector = this.connectors.find((entry) => entry.spec.id === num(values, 'id'))
        if (connector) connector.spec = { ...connector.spec, isSuspended: command.id.endsWith('_suspend_t') }
        return
      }
      case 'packet_settings_logs_set_t':
        this.logs = { level: num(values, 'level'), mirrorSerial: num(values, 'mirror_serial') !== 0, traceErrors: num(values, 'trace_errors') !== 0 }
        return
      default:
        this.warn(`${command.id} is not decoded; it isn't kept.`)
    }
  }

  private serviceByUuid(uuid: number) {
    return this.services.find((entry) => uuidOf(entry.spec.uuid) === uuid)
  }

  result(): SettingsState {
    return {
      services: this.services.map((entry) => ({ ...entry.spec, characteristics: entry.chars })),
      connectors: this.connectors.map((entry) => ({ ...entry.spec, bindings: this.bindingRows(entry) })),
      logs: this.logs,
    }
  }

  /** One row per provider + endpoint; TX and RX on the same endpoint become TX_RX. */
  private bindingRows(entry: MutableConnector): ConnectorBindingSpec[] {
    const rows = new Map<string, ConnectorBindingSpec>()
    for (const [key, endpoint] of entry.bindings) {
      const [direction, providerId] = key.split(':') as [Direction, string]
      const provider = this.providerName(Number(providerId))
      if (!provider) {
        this.warn(`Connector '${entry.spec.name}' binds provider ${providerId}, which the app doesn't know.`)
        continue
      }
      const row = `${provider}:${endpoint}`
      const existing = rows.get(row)
      const endpointText = entry.texts.get(`${key}:${endpoint}`) ?? (provider === 'BLE' ? hex(endpoint, 4) : String(endpoint))
      rows.set(row, {
        id: `b_${entry.spec.key}_${provider.toLowerCase()}_${endpoint}`,
        provider,
        endpoint: existing?.endpoint ?? endpointText,
        direction: existing && existing.direction !== direction ? 'TX_RX' : direction,
      })
    }
    return [...rows.values()]
  }
}

const uuidOf = (textValue: string): number => Number.parseInt(textValue.trim().replace(/^0x/i, ''), 16)

const directionsOf = (binding: ConnectorBindingSpec): Direction[] => (binding.direction === 'TX_RX' ? ['TX', 'RX'] : [binding.direction])

const parseEndpoint = (binding: ConnectorBindingSpec): number | undefined => {
  if (binding.provider === 'BLE') {
    const value = uuidOf(binding.endpoint)
    return Number.isInteger(value) ? value : undefined
  }
  const match = /\((\d+)\)\s*$/.exec(binding.endpoint) ?? /^\s*(\d+)\s*$/.exec(binding.endpoint)
  return match ? Number(match[1]) : undefined
}

// ---------------------------------------------------------------------------
// VM program (0x04)
// ---------------------------------------------------------------------------

interface VmObjectRecord {
  readonly type: VmObjectType
  readonly elements: number
  readonly name: string
  readonly mutable: boolean
  readonly retentive: boolean
  readonly blockOwned: boolean
}

class VmReplay {
  objects = new Map<number, VmObjectRecord>()
  /** Wire bytes per object, and how many elements the records wrote (values are sent up to the last written one). */
  data = new Map<number, { bytes: Uint8Array; written: number }>()
  subscribed: readonly number[] = []
  count = 0
  accessors = new Map<number, RawAccessor>()
  rawBlocks: RawBlock[] = []

  private readonly ctx: StoredCodeContext
  private readonly diagnostics: UploadDiagnostic[]

  constructor(ctx: StoredCodeContext, diagnostics: UploadDiagnostic[]) {
    this.ctx = ctx
    this.diagnostics = diagnostics
  }

  private records(frame: Uint8Array, packet: 'addObjects' | 'setData' | 'subscribe' | 'addAccessors') {
    const countBytes = this.ctx.vm.wire[packet].batch?.countBytes ?? 1
    const view = new DataView(frame.buffer, frame.byteOffset, frame.byteLength)
    const count = countBytes === 2 ? view.getUint16(2, true) : frame[2]!
    return { view, count, offset: 2 + countBytes }
  }

  apply(frame: Uint8Array): boolean {
    const { vm } = this.ctx
    const packet = frame[1]
    if (packet === vm.packets.reset || packet === vm.packets.open) {
      this.objects = new Map()
      this.data = new Map()
      this.subscribed = []
      this.accessors = new Map()
      this.rawBlocks = []
      this.count = packet === vm.packets.open ? new DataView(frame.buffer, frame.byteOffset).getUint16(2, true) : 0
      return true
    }
    if (packet === vm.packets.addObjects) {
      const { view, count } = this.records(frame, 'addObjects')
      let { offset } = this.records(frame, 'addObjects')
      for (let index = 0; index < count; index++) {
        const id = view.getUint16(offset, true)
        const head = decodeVmObjectHead(vm, frame.subarray(offset + 2, offset + 2 + vm.head.size))
        const nameSize = head['d.name_size']!
        const nameStart = offset + 2 + vm.head.size
        const name = String.fromCharCode(...frame.subarray(nameStart, nameStart + nameSize))
        offset = nameStart + nameSize
        const type = vm.types.find((entry) => entry.value === head['d.obj_t'])
        if (!type) {
          this.diagnostics.push({ severity: 'error', message: `Object ${id} has type ${head['d.obj_t']}, which the descriptors don't know.` })
          continue
        }
        this.objects.set(id, {
          type,
          elements: Math.floor(head.payload_size! / type.memoryWidth),
          name,
          mutable: head['f.mutable'] === 1,
          retentive: head['f.retentive'] === 1,
          blockOwned: head['f.upd_resetable'] === 1,
        })
      }
      return true
    }
    if (packet === vm.packets.setData) {
      const { view, count } = this.records(frame, 'setData')
      let { offset } = this.records(frame, 'setData')
      for (let index = 0; index < count; index++) {
        const id = view.getUint16(offset, true)
        const start = view.getUint16(offset + 2, true)
        const length = view.getUint16(offset + 4, true)
        const chunk = frame.subarray(offset + 6, offset + 6 + length)
        offset += 6 + length
        const object = this.objects.get(id)
        if (!object) {
          this.diagnostics.push({ severity: 'error', message: `Values for object ${id}, which isn't created before them.` })
          continue
        }
        const width = object.type.wireWidth
        const entry = this.data.get(id) ?? { bytes: new Uint8Array(object.elements * width), written: 0 }
        entry.bytes.set(chunk.subarray(0, Math.max(0, entry.bytes.byteLength - start * width)), start * width)
        entry.written = Math.max(entry.written, start + length / width)
        this.data.set(id, entry)
      }
      return true
    }
    if (packet === vm.packets.addAccessors) {
      const { view, count } = this.records(frame, 'addAccessors')
      let { offset } = this.records(frame, 'addAccessors')
      const { literal, ref, name } = vm.indexKinds
      for (let index = 0; index < count; index++) {
        const id = view.getUint16(offset, true)
        const root = view.getUint16(offset + 2, true)
        const stepCount = frame[offset + 4]!
        const size = frame[offset + 5]!
        let at = offset + vm.wire.addAccessors.recordSize
        const end = at + size
        const steps: RawStep[] = []
        for (let step = 0; step < stepCount && at < end; step++) {
          const kind = frame[at]
          if (kind === literal.value) {
            steps.push({ kind: 'literal', value: view.getUint32(at + 1, true) })
            at += literal.size
          } else if (kind === ref.value) {
            steps.push({ kind: 'ref', accessor: view.getUint16(at + 1, true) })
            at += ref.size
          } else if (kind === name.value) {
            const length = frame[at + 1]!
            steps.push({ kind: 'name', name: String.fromCharCode(...frame.subarray(at + name.size, at + name.size + length)) })
            at += name.size + length
          } else {
            this.diagnostics.push({ severity: 'error', message: `Accessor ${id} has a step of kind ${kind}, which the descriptors don't know.` })
            break
          }
        }
        this.accessors.set(id, { root, steps })
        offset = end
      }
      return true
    }
    if (packet === vm.packets.addBlock) {
      const view = new DataView(frame.buffer, frame.byteOffset, frame.byteLength)
      const head = 2 + vm.wire.addBlock.recordSize
      const inCount = frame[2 + 5]!
      const outCount = frame[2 + 6]!
      const enCount = frame[2 + 7]!
      const customLength = view.getUint16(2 + 10, true)
      const pins = Array.from({ length: inCount + outCount + enCount }, (_, index) => view.getUint16(head + index * 2, true))
      const customAt = head + pins.length * 2
      this.rawBlocks.push({
        wireId: view.getUint16(2, true),
        typeId: frame[2 + 4]!,
        inCount,
        outCount,
        enCount,
        enMode: frame[2 + 8]!,
        onError: frame[2 + 9]!,
        eno: view.getUint16(2 + 12, true),
        pins,
        custom: frame.slice(customAt, customAt + customLength),
      })
      return true
    }
    if (packet === vm.packets.subscribe) {
      const { view, count, offset } = this.records(frame, 'subscribe')
      this.subscribed = Array.from({ length: count }, (_, index) => view.getUint16(offset + index * vm.wire.subscribe.recordSize, true))
      return true
    }
    return false
  }

  /** A folder's entries (object IDs), in order. */
  private children(wireId: number): number[] {
    const entry = this.data.get(wireId)
    if (!entry) return []
    const view = new DataView(entry.bytes.buffer)
    return Array.from({ length: entry.written }, (_, index) => view.getUint16(index * 2, true))
  }

  /** The type of the object an accessor's path ends on (through fixed folder positions), when the frames say. */
  private typeOfAccessor(accessorId: number): string | undefined {
    const accessor = this.accessors.get(accessorId)
    if (!accessor) return undefined
    let id = accessor.root
    for (const step of accessor.steps) {
      if (this.objects.get(id)?.type.key !== this.ctx.vm.ptrType.key || step.kind !== 'literal') break
      const child = this.children(id)[step.value]
      if (child === undefined) return undefined
      id = child
    }
    return this.objects.get(id)?.type.key
  }

  /** Objects back into trees, the way the compiler numbered them: a folder, then its own children, depth first; and the blocks. */
  result(): { user: ObjectNode[]; blocks: ObjectNode[]; program: ProgramBlock[] } {
    const nodeId = (wireId: number) => `obj-${wireId}`
    const subscribed = new Set(this.subscribed)
    const placed = new Set<number>()
    let next = 0
    const children = (wireId: number) => this.children(wireId)

    // With blocks in the frames, only nameless tail objects no folder holds are the blocks' own (a user object a block drives is flagged too).
    const withBlocks = this.rawBlocks.length > 0
    const inFolder = new Set<number>()
    for (const [id, object] of this.objects) if (object.type.key === this.ctx.vm.ptrType.key) for (const child of children(id)) inFolder.add(child)
    const ids = [...this.objects.keys()].sort((left, right) => left - right)
    let boundary = Number.POSITIVE_INFINITY
    for (let index = ids.length - 1; index >= 0 && this.objects.get(ids[index]!)!.blockOwned; index--) boundary = ids[index]!
    const owned = (wireId: number): boolean => {
      const object = this.objects.get(wireId)
      if (!object) return false
      return withBlocks ? object.blockOwned && object.name === '' && wireId >= boundary && !inFolder.has(wireId) : object.blockOwned
    }

    const place = (wireId: number): ObjectNode => {
      placed.add(wireId)
      next = wireId + 1
      const object = this.objects.get(wireId)!
      const common = { id: nodeId(wireId), name: object.name, ...(subscribed.has(wireId) ? { subscribed: true } : {}) }
      if (object.type.key === this.ctx.vm.ptrType.key) {
        const nodes = children(wireId).map((child, index): ObjectNode => {
          // Owned when numbered right after the folder's subtree so far, and in the same section (block-owned or not).
          if (child === next && !placed.has(child) && this.objects.has(child) && owned(child) === owned(wireId)) return place(child)
          return { kind: 'reference', id: `ref-${wireId}-${index}`, name: this.objects.get(child)?.name ?? '', targetId: nodeId(child) }
        })
        const folder: FolderNode = { kind: 'folder', ...common, children: nodes }
        return folder
      }
      const data = this.data.get(wireId)
      const value = data ? decodeVmElements(object.type, data.bytes.subarray(0, data.written * object.type.wireWidth)) : undefined
      const node: ValueNode = {
        kind: 'value',
        ...common,
        type: object.type.key,
        length: object.elements,
        mutable: object.mutable,
        retentive: object.retentive,
        ...(value === undefined ? {} : { value }),
      }
      return node
    }

    let user: ObjectNode[] = []
    let blocks: ObjectNode[] = []
    const total = Math.max(this.count, ...[...this.objects.keys()].map((id) => id + 1))
    while (next < total) {
      if (!this.objects.has(next)) {
        this.diagnostics.push({ severity: 'warning', message: `Object ${next} is never created; its ID stays empty.` })
        next++
        continue
      }
      ;(owned(next) ? blocks : user).push(place(next))
    }

    let program: ProgramBlock[] = []
    if (withBlocks) {
      const recovery = recoverBlocks({
        vm: this.ctx.vm,
        blocks: this.rawBlocks,
        accessors: this.accessors,
        objectId: nodeId,
        isOwned: owned,
        typeOfAccessor: (id) => this.typeOfAccessor(id),
        diagnostics: this.diagnostics,
      })
      program = [...recovery.blocks]
      // The blocks make their own objects again when compiled; keep only what no block owns.
      blocks = blocks.filter((node) => !recovery.ownedPins.has(Number(node.id.slice(4))))
    }
    return { user, blocks, program }
  }
}

// ---------------------------------------------------------------------------

export const decodeStoredCode = (frames: readonly Uint8Array[], boardDefaults: SettingsState, ctx: StoredCodeContext, name = 'Recovered'): RecoveredCode => {
  const diagnostics: UploadDiagnostic[] = []
  const settings = new SettingsReplay(boardDefaults, ctx, diagnostics)
  const vm = new VmReplay(ctx, diagnostics)
  const extraFrames: StoredCodeStep[] = []
  const devices: ProjectDevice[] = []
  const setup: ActionStep[] = []
  const commands = new Map(ctx.commands.commands.map((command) => [(command.classHeader << 8) | command.packetHeader, command]))
  const settingsClasses = new Set(ctx.commands.groups.filter((group) => group.id === 'ble' || group.id === 'data-connector' || group.id === 'logs').map((group) => group.classHeader))

  for (const [index, frame] of frames.entries()) {
    const cls = frame[0]!
    if (cls === ctx.vm.classHeader) {
      if (!vm.apply(frame)) diagnostics.push({ severity: 'warning', message: `Frame ${index}: VM packet 0x${frame[1]!.toString(16)} isn't decoded and isn't kept.` })
      continue
    }
    if (settingsClasses.has(cls)) {
      const command = commands.get((cls << 8) | frame[1]!)
      if (!command) {
        diagnostics.push({ severity: 'warning', message: `Frame ${index}: settings packet 0x${cls.toString(16)}/0x${frame[1]!.toString(16)} is unknown to the descriptors.` })
        continue
      }
      try {
        settings.apply(command, unpackStruct(command.request.wire, frame.subarray(2)).values)
      } catch (error) {
        diagnostics.push({ severity: 'error', message: `Frame ${index} (${command.name}): ${error instanceof Error ? error.message : String(error)}` })
      }
      continue
    }
    const deviceType = ctx.devices.typeByInstall(cls, frame[1]!)
    if (deviceType) {
      try {
        const { device_id: deviceId, ...install } = unpackStruct(deviceType.install.request.wire, frame.subarray(2)).values
        const same = devices.filter((device) => device.type === deviceType.id).length
        devices.push({
          id: `device-${devices.length + 1}`,
          deviceId: Number(deviceId),
          type: deviceType.id,
          name: same ? `${deviceType.title} ${same + 1}` : deviceType.title,
          tags: [],
          install: Object.fromEntries(Object.entries(install).map(([key, value]) => [key, Number(value)])),
        })
      } catch (error) {
        diagnostics.push({ severity: 'error', message: `Frame ${index} (${deviceType.install.name}): ${error instanceof Error ? error.message : String(error)}` })
      }
      continue
    }
    const command = commands.get((cls << 8) | frame[1]!)
    // A contract call on a device the code knows: a default setting.
    if (command?.group.source === 'contracts' && command.request.fields[0]?.name === 'device_id') {
      try {
        const { device_id: id, ...values } = unpackStruct(command.request.wire, frame.subarray(2)).values
        const deviceId = Number(id)
        const ref = ctx.devices.board.some((entry) => entry.deviceId === deviceId) ? boardDeviceRef(deviceId) : devices.find((entry) => entry.deviceId === deviceId)?.id
        const numeric = Object.values(values).every((value) => typeof value === 'number' || (Array.isArray(value) && value.every((item) => typeof item === 'number')))
        if (ref && numeric) {
          setup.push({ id: `setup-${setup.length + 1}`, device: ref, contract: command.id, values: values as Record<string, number | number[]> })
          continue
        }
      } catch {
        /* Kept as an extra frame below. */
      }
    }
    extraFrames.push({ label: command ? command.name : `0x${cls.toString(16)}/0x${frame[1]!.toString(16)}`, frame })
  }

  const trees = vm.result()
  if (trees.program.length) {
    // The canvas orders blocks by its wires and places; say so when that would not give the stored order back.
    const placedBlocks = placeBlocks(trees.program, () => ({ width: 200, height: 100 }))
    const order = arrangeProgram([{ id: 'recovered', name: 'Main', blocks: placedBlocks }]).blocks.map((block) => block.id)
    if (order.some((id, position) => id !== trees.program[position]!.id)) {
      diagnostics.push({ severity: 'warning', message: 'The canvas orders the recovered blocks by their wires, which differs from the stored order (blocks that share no wire may run in another order).' })
    }
  }
  return {
    project: { ...createProject(name), objects: trees.user },
    sections: trees.blocks.length ? [{ key: 'blocks', owner: 'block', objects: trees.blocks }] : [],
    blocks: trees.program,
    settings: settings.result(),
    devices,
    setup,
    extraFrames,
    diagnostics,
  }
}
