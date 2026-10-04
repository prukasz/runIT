import { connectionProblem } from './connect'
import { BOARD_REF, endpointKey, flowOf, isSupply, sameEndpoint } from './hardware'
import type { Endpoint, NetSpec, PinSpec, Wire } from './hardware'
import { buildNets, netVoltage } from './nets'
import type { WiringNet } from './nets'

/*
 * What the wires mean: the electrical dependencies of a design (a pin needs a connection that fits it, a net needs one
 * source, a powered device needs ground, a limit is not exceeded) and the link from a wire to a device's install
 * frame (a wire to a board GPIO is that device's pin ref).
 */

export type IssueSeverity = 'error' | 'warning' | 'info'

export interface WiringIssue {
  readonly severity: IssueSeverity
  readonly code: string
  readonly message: string
  /** The device (project ID or `board`) and pin the issue is about. */
  readonly device?: string
  readonly pin?: string
  readonly wire?: string
  readonly net?: string
}

/** A device on the canvas with the pins its variant brings out. */
export interface WiringDevice {
  readonly ref: string
  readonly name: string
  readonly pins: ReadonlyMap<string, PinSpec>
}

export interface WiringContext {
  readonly nets: Readonly<Record<string, NetSpec>>
  /** The board's terminals by art ID. */
  readonly board: ReadonlyMap<string, PinSpec>
  readonly devices: readonly WiringDevice[]
}

export const specOf = (context: WiringContext, endpoint: Endpoint): PinSpec | undefined =>
  endpoint.device === BOARD_REF ? context.board.get(endpoint.pin) : context.devices.find((device) => device.ref === endpoint.device)?.pins.get(endpoint.pin)

export const labelOf = (context: WiringContext, endpoint: Endpoint): string => {
  const spec = specOf(context, endpoint)
  const pin = spec?.label ?? endpoint.pin
  return endpoint.device === BOARD_REF ? `${pin} (board)` : `${pin} of ${context.devices.find((device) => device.ref === endpoint.device)?.name ?? endpoint.device}`
}

export const netContext = (context: WiringContext) => ({
  specOf: (endpoint: Endpoint) => specOf(context, endpoint),
  terminalsOf: (net: string): Endpoint[] => [...context.board].filter(([, spec]) => spec.net === net).map(([pin]) => ({ device: BOARD_REF, pin })),
  nets: context.nets,
})

const volts = (mV: number): string => `${Number((mV / 1000).toFixed(2))} V`
const range = (min: number | undefined, max: number | undefined): string => (min !== undefined && max !== undefined ? `${volts(min)} to ${volts(max)}` : min !== undefined ? `from ${volts(min)}` : max !== undefined ? `up to ${volts(max)}` : '')

/** Everything wrong or doubtful about a set of wires. */
export function checkWiring(wires: readonly Wire[], context: WiringContext): WiringIssue[] {
  const issues: WiringIssue[] = []
  const usable: Wire[] = []
  const seen = new Set<string>()

  // 1. Each wire on its own: both ends exist and fit.
  for (const wire of wires) {
    const a = specOf(context, wire.a)
    const b = specOf(context, wire.b)
    const missing = !a ? wire.a : !b ? wire.b : undefined
    if (missing) {
      issues.push({ severity: 'error', code: 'dangling', message: `A wire ends on ${labelOf(context, missing)}, which is not there (the device or its variant does not bring that pin out).`, wire: wire.id, device: missing.device, pin: missing.pin })
      continue
    }
    if (sameEndpoint(wire.a, wire.b)) {
      issues.push({ severity: 'error', code: 'self', message: `A wire joins ${labelOf(context, wire.a)} to itself.`, wire: wire.id })
      continue
    }
    const key = [endpointKey(wire.a), endpointKey(wire.b)].sort().join('->')
    if (seen.has(key)) {
      issues.push({ severity: 'warning', code: 'duplicate', message: `${labelOf(context, wire.a)} and ${labelOf(context, wire.b)} are wired twice.`, wire: wire.id })
      continue
    }
    seen.add(key)
    const problem = connectionProblem(a as PinSpec, b as PinSpec)
    if (problem) issues.push({ severity: 'error', code: 'incompatible', message: `${labelOf(context, wire.a)} to ${labelOf(context, wire.b)}: ${problem}`, wire: wire.id, device: wire.a.device, pin: wire.a.pin })
    usable.push(wire)
  }

  // 2. Each net: one source, no shorts, voltages within limits, something drives an input.
  const nets = buildNets(usable, netContext(context))
  // An issue about one pin sits on the wire at that pin (a board terminal reached only through its net: the net's first wire).
  const wireAt = (endpoint: Endpoint): string | undefined => usable.find((wire) => sameEndpoint(wire.a, endpoint) || sameEndpoint(wire.b, endpoint))?.id
  for (const net of nets) issues.push(...checkNet(net, context, wireAt))

  // 3. Each wired device: powered, grounded, a complete bus.
  for (const device of context.devices) {
    const wired = new Set(usable.flatMap((wire) => [wire.a, wire.b]).filter((endpoint) => endpoint.device === device.ref).map((endpoint) => endpoint.pin))
    if (!wired.size) continue
    for (const [pin, spec] of device.pins) {
      if (wired.has(pin)) continue
      if (spec.kind === 'supply_in') issues.push({ severity: 'warning', code: 'unpowered', message: `${device.name}: ${spec.label} is not connected to a supply.`, device: device.ref, pin })
      else if (spec.kind === 'gnd') issues.push({ severity: 'warning', code: 'no-ground', message: `${device.name}: ${spec.label} is not connected to ground.`, device: device.ref, pin })
    }
    const buses = [...device.pins].filter(([, spec]) => spec.kind === 'bus' && spec.signal?.startsWith('i2c-'))
    if (buses.some(([pin]) => wired.has(pin))) {
      for (const [pin, spec] of buses) {
        if (!wired.has(pin)) issues.push({ severity: 'warning', code: 'i2c-incomplete', message: `${device.name}: I2C needs ${spec.label} wired as well.`, device: device.ref, pin })
      }
    }
  }

  const order = { error: 0, warning: 1, info: 2 }
  return issues.sort((x, y) => order[x.severity] - order[y.severity])
}

function checkNet(net: WiringNet, context: WiringContext, wireAt: (endpoint: Endpoint) => string | undefined): WiringIssue[] {
  const issues: WiringIssue[] = []
  const specs = net.endpoints.map((endpoint) => ({ endpoint, spec: specOf(context, endpoint) })).filter((entry): entry is { endpoint: Endpoint; spec: PinSpec } => !!entry.spec)
  const here = { net: net.id, wire: net.wires[0], device: net.endpoints[0]?.device, pin: net.endpoints[0]?.pin }

  const supplyNames = net.named.filter((name) => context.nets[name]?.kind === 'supply')
  if (supplyNames.length > 1) issues.push({ severity: 'error', code: 'short', message: `${supplyNames.map((name) => context.nets[name].label).join(' and ')} are joined: that is a short between two supplies.`, ...here })
  if (net.named.includes('GND') && supplyNames.length) issues.push({ severity: 'error', code: 'short', message: `${context.nets[supplyNames[0]].label} is joined to ground: that is a short.`, ...here })

  if (net.role === 'power') {
    // One source per net: a named supply counts once, each unnamed supply output on its own (two named ones are a short, above).
    const outputs = specs.filter(({ spec }) => spec.kind === 'supply_out')
    const sources = new Set(outputs.map(({ endpoint, spec }) => spec.net ?? endpointKey(endpoint)))
    if (!outputs.length) issues.push({ severity: 'warning', code: 'no-source', message: 'Nothing powers this net: no supply output is on it.', ...here })
    else if (sources.size > 1 && supplyNames.length < 2) issues.push({ severity: 'error', code: 'two-sources', message: `Two supplies drive one net (${outputs.map(({ endpoint }) => labelOf(context, endpoint)).slice(0, 3).join(', ')}).`, ...here })

    const voltage = netVoltage(net, context.nets)
    for (const { endpoint, spec } of specs) {
      if (spec.kind !== 'supply_in' && spec.kind !== 'io' && spec.kind !== 'bus') continue
      if (voltage) {
        if (spec.mV_max !== undefined && voltage.max > spec.mV_max) issues.push({ severity: 'error', code: 'overvoltage', message: `${labelOf(context, endpoint)} accepts up to ${volts(spec.mV_max)} but this net can reach ${volts(voltage.max)}.`, net: net.id, device: endpoint.device, pin: endpoint.pin, wire: wireAt(endpoint) ?? net.wires[0] })
        if (spec.mV_min !== undefined && voltage.min < spec.mV_min) issues.push({ severity: 'error', code: 'undervoltage', message: `${labelOf(context, endpoint)} needs at least ${volts(spec.mV_min)} but this net can drop to ${volts(voltage.min)}.`, net: net.id, device: endpoint.device, pin: endpoint.pin, wire: wireAt(endpoint) ?? net.wires[0] })
      } else if (spec.kind === 'supply_in' && (spec.mV_min !== undefined || spec.mV_max !== undefined) && net.named.length === 1) {
        issues.push({ severity: 'info', code: 'regulator-setting', message: `${context.nets[net.named[0]].label} is set at runtime: keep it ${range(spec.mV_min, spec.mV_max)} for ${labelOf(context, endpoint)}.`, net: net.id, device: endpoint.device, pin: endpoint.pin, wire: wireAt(endpoint) ?? net.wires[0] })
      }
    }
  }

  if (net.role === 'signal') {
    const producers = specs.filter(({ spec }) => flowOf(spec) === 'producer')
    const flexible = specs.filter(({ spec }) => flowOf(spec) === 'both')
    const consumers = specs.filter(({ spec }) => flowOf(spec) === 'consumer')
    if (producers.length > 1) issues.push({ severity: 'error', code: 'two-drivers', message: `Two producers drive one net (${producers.map(({ endpoint }) => labelOf(context, endpoint)).join(', ')}).`, ...here })
    // A pin that can be either has to be set up as the one the net needs: the dependency of the wire on the pin's mode.
    if (consumers.length && !producers.length && flexible.length === 1) {
      const { endpoint } = flexible[0]
      issues.push({ severity: 'info', code: 'set-output', message: `${labelOf(context, endpoint)} is the only producer on this net: set it as an output.`, net: net.id, device: endpoint.device, pin: endpoint.pin, wire: wireAt(endpoint) ?? net.wires[0] })
    }
    if (producers.length && !consumers.length && flexible.length) {
      for (const { endpoint } of flexible) issues.push({ severity: 'info', code: 'set-input', message: `${labelOf(context, endpoint)} receives from ${labelOf(context, producers[0].endpoint)}: set it as an input.`, net: net.id, device: endpoint.device, pin: endpoint.pin, wire: wireAt(endpoint) ?? net.wires[0] })
    }
    if (consumers.length && !producers.length && !flexible.length) issues.push({ severity: 'warning', code: 'no-driver', message: `${labelOf(context, consumers[0].endpoint)} is a consumer with no producer on its net.`, net: net.id, device: consumers[0].endpoint.device, pin: consumers[0].endpoint.pin, wire: wireAt(consumers[0].endpoint) ?? net.wires[0] })
  }
  return issues
}

/** A device pin wired to a board terminal that is a sys_io pin: the install field the wire implies. */
export interface ConfigLink {
  /** The user device (project ID) and the install pin-ref field (the pins csv `cfg_field`). */
  readonly device: string
  readonly field: string
  /** The board pin the wire reaches: device ID and pin number. */
  readonly sysPin: { readonly device: number; readonly pin: number }
  readonly wire: string
  /** Which board terminal and which device pin the wire joins, for messages. */
  readonly terminal: string
  readonly pin: string
}

export function configLinks(wires: readonly Wire[], context: WiringContext): ConfigLink[] {
  const links: ConfigLink[] = []
  for (const wire of wires) {
    for (const [device, board] of [[wire.a, wire.b], [wire.b, wire.a]] as const) {
      if (device.device === BOARD_REF || board.device !== BOARD_REF) continue
      const spec = specOf(context, device)
      const terminal = context.board.get(board.pin)
      if (spec?.cfgField && terminal?.sysPin) links.push({ device: device.device, field: spec.cfgField, sysPin: terminal.sysPin, wire: wire.id, terminal: board.pin, pin: device.pin })
    }
  }
  return links
}

export { isSupply }

/** `PIN (producer)`: a pin named with its role, for messages. */
export const describeEnd = (context: WiringContext, endpoint: Endpoint): string => {
  const spec = specOf(context, endpoint)
  return `${labelOf(context, endpoint)}${spec ? ` (${flowOf(spec) === 'both' ? 'either' : flowOf(spec)})` : ''}`
}
