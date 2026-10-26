import { bundledEnums } from '@backend'
import type { BoardDescriptor, FieldValues, PinRef, ServiceCatalog } from '@backend'
import { baseConfig, deviceList, nextDeviceId } from '../devices/devices'
import type { ProjectDevice, ProjectFile, Step } from '../project/projectFile'
import type { HardwareItem, HardwareLayout } from './layout'
import { EMPTY_HARDWARE, freshId } from './layout'
import type { Library, LibraryPart, Rotation } from './library'
import { withProjectParts } from './library'
import { buildNetlist, busField, groupOf } from './netlist'
import type { HardwareContext, Netlist, PinNode } from './netlist'
import { solvePower } from './power'
import type { PowerResult } from './power'

/*
 * What the Devices canvas decides, written into the project (pure): a part with a device is a project device (made when the part
 * is placed, removed with it); a config pin of its device is the pin it is wired to (a servo's PWM pin), an I2C bus its device's
 * bus, a DC motor's driver and channel the outputs its leads go to; and for the board's power, the start commands of the regulators the canvas sets (POWER_VREG VOLTAGE and ENABLE) and of the
 * muxes it picks (POWER_SUPPLY SOURCE). Those devices are `managed`: their such steps are the canvas's, replaced at each change,
 * removed when nothing on the canvas needs them. Every edit of the canvas goes through `applyHardware`, so it is one undo step.
 */

export interface ApplyContext {
  readonly catalog: ServiceCatalog
  readonly board: BoardDescriptor
  readonly library: Library
  /** IDs the board answered for (never given to a new device). */
  readonly occupied?: ReadonlySet<number>
}

export const hardwareOf = (project: ProjectFile): HardwareLayout => project.hardware ?? EMPTY_HARDWARE

/** The library with the project's own parts, and the devices: what the canvas's rules read. */
export const contextOf = (project: ProjectFile, a: ApplyContext): HardwareContext => ({
  library: withProjectParts(a.library, hardwareOf(project).parts),
  board: a.board,
  devices: deviceList(project, a.catalog, a.board),
})

/** The steps the canvas writes, by contract and command. */
const MANAGED_COMMANDS = new Set(['POWER_VREG/VOLTAGE', 'POWER_VREG/ENABLE', 'POWER_SUPPLY/SOURCE'])
const isManagedStep = (s: Step): boolean => s.kind === 'device' && MANAGED_COMMANDS.has(`${s.contract}/${s.command}`)

const supplyMember = (input: number): string => {
  // SOURCE takes Off, Source A, Source B (sys_power_supply_source_e): input 0 is A.
  const members = bundledEnums().sys_power_supply_source_e?.members ?? []
  return members.find((m) => m.value === input + 1)?.name ?? String(input + 1)
}

/** The start commands of the board's devices the power choices give: device -> steps. */
export const powerSteps = (power: PowerResult): Map<number, Step[]> => {
  const out = new Map<number, Step[]>()
  for (const state of power.elements.values()) {
    const { element } = state
    if (element.device === undefined || !(state.lockedBy?.length || state.byUser)) continue
    if (element.kind === 'regulator' && element.adjustable && state.setpoint_mV !== undefined) {
      out.set(element.device, [
        { kind: 'device', device: element.device, contract: 'POWER_VREG', command: 'VOLTAGE', args: { voltage_mV: state.setpoint_mV } },
        { kind: 'device', device: element.device, contract: 'POWER_VREG', command: 'ENABLE', args: { enable: 1 } },
      ])
    }
    if (element.kind === 'mux' && state.input !== undefined) {
      out.set(element.device, [{ kind: 'device', device: element.device, contract: 'POWER_SUPPLY', command: 'SOURCE', args: { source: supplyMember(state.input) } }])
    }
  }
  return out
}

/** What a part's device starts with: the user bus, the preset's values, the part's own. */
const partConfig = (part: LibraryPart, a: ApplyContext): FieldValues => {
  const type = part.device ? a.catalog.devices.get(part.device.type) : undefined
  const preset = type?.presets?.find((p) => p.id === part.device?.preset)?.values ?? {}
  return { ...baseConfig(type, a.board), ...preset, ...(part.device?.config as FieldValues | undefined) }
}

/** Adds a part to the canvas, turned by `rotation` (and its device to the project). Returns the project and the new item's id. */
export const placePart = (project: ProjectFile, part: LibraryPart, at: { x: number; y: number }, a: ApplyContext, rotation: Rotation = 0): { project: ProjectFile; item: string } => {
  const hw = hardwareOf(project)
  const id = freshId(hw.items.map((i) => i.id), 'part')
  let devices = project.devices
  let device: number | undefined
  if (part.device && a.catalog.devices.has(part.device.type)) {
    device = nextDeviceId(project, a.board, a.occupied)
    const taken = new Set(project.devices.map((d) => d.name))
    let n = 1
    while (taken.has(`${part.name} ${n}`)) n++
    const created: ProjectDevice = { id: device, type: part.device.type, name: `${part.name} ${n}`, config: partConfig(part, a), defaults: [] }
    devices = [...devices, created]
  }
  // A part made in the creator travels in the project.
  const parts = part.origin === 'project' && !hw.parts.some((p) => p.id === part.id) ? [...hw.parts, part] : hw.parts
  const item: HardwareItem = { id, part: part.id, x: at.x, y: at.y, rotation, ...(device !== undefined ? { device } : {}) }
  return { project: applyHardware({ ...project, devices, hardware: { ...hw, parts, items: [...hw.items, item] } }, a), item: id }
}

/** Removes parts from the canvas, their wires and their devices. */
export const removeItems = (project: ProjectFile, ids: readonly string[], a: ApplyContext): ProjectFile => {
  const hw = hardwareOf(project)
  const gone = new Set(ids)
  const devices = new Set(hw.items.filter((i) => gone.has(i.id) && i.device !== undefined).map((i) => i.device!))
  return applyHardware({
    ...project,
    devices: project.devices.filter((d) => !devices.has(d.id)),
    hardware: { ...hw, items: hw.items.filter((i) => !gone.has(i.id)), wires: hw.wires.filter((w) => !gone.has(w.a.item) && !gone.has(w.b.item)) },
  }, a)
}

/** A device removed elsewhere (the Devices view): its part leaves the canvas with its wires. */
export const forgetDevice = (hw: HardwareLayout | undefined, device: number): HardwareLayout | undefined => {
  if (!hw) return hw
  const gone = new Set(hw.items.filter((i) => i.device === device).map((i) => i.id))
  if (gone.size === 0) return hw
  return { ...hw, items: hw.items.filter((i) => !gone.has(i.id)), wires: hw.wires.filter((w) => !gone.has(w.a.item) && !gone.has(w.b.item)) }
}

/**
 * Where a DC motor is, from its leads (`a` given): both on outputs of one driver (OUT1 + OUT2 is its channel 0, OUT3 + OUT4
 * channel 1), or one on an output and the other to ground (a half bridge: that output, one way). Wired otherwise: nothing.
 */
const motorLeads = (net: Netlist, a: PinNode): { bridge: number; channel: number; one_way: number } | undefined => {
  const b = [...net.nodes.values()].find((n) => n.end.item === a.end.item && n.kind === 'motor' && n.pin.motor === 'b')
  if (!b) return undefined
  const output = (lead: PinNode) => groupOf(net, lead.key).find((m) => m.kind === 'hbridge')?.pin.hbridge
  const grounded = (lead: PinNode) => groupOf(net, lead.key).some((m) => m.kind === 'gnd')
  const [oa, ob] = [output(a), output(b)]
  if (oa && ob && oa.device === ob.device) return { bridge: oa.device, channel: Math.floor(Math.min(oa.channel, ob.channel) / 2), one_way: 0 }
  const half = oa && grounded(b) ? oa : ob && grounded(a) ? ob : undefined
  return half ? { bridge: half.device, channel: half.channel, one_way: 1 } : undefined
}

const samePin = (a: unknown, b: PinRef | null): boolean =>
  (a === null || a === undefined) ? b === null : b !== null && typeof a === 'object' && (a as PinRef).device === b.device && (a as PinRef).pin === b.pin

/**
 * The project with what the canvas decides: each part's device config from its wires, and the managed start commands of the
 * board's power devices. Changes nothing else.
 */
export const applyHardware = (project: ProjectFile, a: ApplyContext): ProjectFile => {
  const hw = hardwareOf(project)
  const ctx = contextOf(project, a)
  const net = buildNetlist(hw, ctx)
  // 1. Device configs from the wires.
  const changes = new Map<number, Record<string, unknown>>()
  for (const node of net.nodes.values()) {
    if (node.device === undefined) continue
    const device = project.devices.find((d) => d.id === node.device)
    if (!device) continue
    const type = a.catalog.devices.get(device.type)
    if (node.kind === 'consumer' && node.pin.config) {
      const provider = groupOf(net, node.key).find((m) => m.kind === 'provider' && m.device !== undefined)
      const value: PinRef | null = provider ? { device: provider.device!, pin: provider.pin.device_pin!.pin } : null
      if (!samePin(device.config?.[node.pin.config], value)) changes.set(device.id, { ...changes.get(device.id), [node.pin.config]: value })
    }
    if (node.kind === 'motor' && node.pin.motor === 'a') {
      const leads = motorLeads(net, node)
      if (leads && Object.entries(leads).some(([k, v]) => device.config?.[k] !== v)) changes.set(device.id, { ...changes.get(device.id), ...leads })
    }
    if (node.kind === 'bus' && node.pin.bus?.type === 'i2c' && node.pin.bus.bus === undefined) {
      const field = busField(type)
      const boardBus = groupOf(net, node.key).find((m) => m.pin.bus?.bus !== undefined)?.pin.bus?.bus
      if (field && boardBus !== undefined && device.config?.[field.id] !== boardBus) changes.set(device.id, { ...changes.get(device.id), [field.id]: boardBus })
    }
  }
  const devices = changes.size === 0 ? project.devices
    : project.devices.map((d) => (changes.has(d.id) ? { ...d, config: { ...d.config, ...changes.get(d.id) } as FieldValues } : d))
  // 2. The power devices' start commands.
  const power = solvePower(net, hw.power)
  const steps = powerSteps(power)
  const managed = new Set([...hw.managed, ...steps.keys()])
  let boardDefaults = project.boardDefaults
  for (const device of managed) {
    const own = (boardDefaults.find((b) => b.device === device)?.steps ?? []).filter((s) => !isManagedStep(s))
    const next = [...own, ...(steps.get(device) ?? [])]
    const others = boardDefaults.filter((b) => b.device !== device)
    boardDefaults = next.length > 0 ? [...others, { device, steps: next }].sort((x, y) => x.device - y.device) : others
  }
  const now = [...steps.keys()].sort((x, y) => x - y)
  const sameSteps = JSON.stringify(boardDefaults) === JSON.stringify(project.boardDefaults)
  const sameManaged = now.length === hw.managed.length && now.every((d, i) => d === hw.managed[i])
  if (devices === project.devices && sameSteps && sameManaged) return project
  return { ...project, devices, boardDefaults: sameSteps ? project.boardDefaults : boardDefaults, hardware: sameManaged ? hw : { ...hw, managed: now } }
}

/** A start command the canvas wrote (shown locked in the Devices view). */
export const isCanvasStep = (project: ProjectFile, device: number, step: Step): boolean => hardwareOf(project).managed.includes(device) && isManagedStep(step)
