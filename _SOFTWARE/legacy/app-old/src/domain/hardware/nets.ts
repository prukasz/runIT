import { endpointKey, isSupply } from './hardware'
import type { Endpoint, NetSpec, PinSpec, Wire } from './hardware'

/*
 * Nets: the groups of pins that are one electrical node. Wires join pins; the terminals of one named board net (every
 * GND terminal, every 3V3 terminal ...) are joined on the PCB, so a wire to any of them reaches the whole net.
 */

export type NetRole = 'ground' | 'power' | 'bus' | 'signal'

export interface WiringNet {
  /** Stable while the net's pins are: the smallest endpoint key. */
  readonly id: string
  /** The wired pins and, for a named board net, its other terminals. */
  readonly endpoints: readonly Endpoint[]
  readonly wires: readonly string[]
  /** The named board nets present (3V3, REG2 ...). */
  readonly named: readonly string[]
  readonly role: NetRole
}

export interface NetContext {
  specOf(endpoint: Endpoint): PinSpec | undefined
  /** Every terminal of a named board net. */
  terminalsOf(net: string): readonly Endpoint[]
  readonly nets: Readonly<Record<string, NetSpec>>
}

export function buildNets(wires: readonly Wire[], context: NetContext): WiringNet[] {
  const parent = new Map<string, string>()
  const endpoints = new Map<string, Endpoint>()
  const find = (key: string): string => {
    let root = key
    while (parent.get(root) !== root) root = parent.get(root) as string
    for (let at = key; at !== root;) {
      const next = parent.get(at) as string
      parent.set(at, root)
      at = next
    }
    return root
  }
  const add = (endpoint: Endpoint): string => {
    const key = endpointKey(endpoint)
    if (!parent.has(key)) {
      parent.set(key, key)
      endpoints.set(key, endpoint)
    }
    return key
  }
  const join = (a: string, b: string) => {
    const rootA = find(a)
    const rootB = find(b)
    if (rootA !== rootB) parent.set(rootB, rootA)
  }

  for (const wire of wires) join(add(wire.a), add(wire.b))
  // The terminals of a named board net a wire reaches are the same node.
  const named = new Set<string>()
  for (const endpoint of [...endpoints.values()]) {
    const net = context.specOf(endpoint)?.net
    if (net) named.add(net)
  }
  for (const net of named) {
    const terminals = context.terminalsOf(net)
    for (const terminal of terminals) join(add(terminals[0]), add(terminal))
  }

  const groups = new Map<string, string[]>()
  for (const key of endpoints.keys()) {
    const root = find(key)
    groups.set(root, [...(groups.get(root) ?? []), key])
  }
  return [...groups.values()].map((keys) => {
    const members = keys.map((key) => endpoints.get(key) as Endpoint)
    const specs = members.map((endpoint) => context.specOf(endpoint)).filter((spec): spec is PinSpec => !!spec)
    const names = [...new Set(specs.map((spec) => spec.net).filter((net): net is string => !!net))].sort()
    const role: NetRole = specs.some((spec) => spec.kind === 'gnd') ? 'ground' : specs.some((spec) => isSupply(spec.kind)) ? 'power' : specs.some((spec) => spec.kind === 'bus') ? 'bus' : 'signal'
    return {
      id: [...keys].sort()[0],
      endpoints: members,
      wires: wires.filter((wire) => keys.includes(endpointKey(wire.a))).map((wire) => wire.id),
      named: names,
      role,
    }
  })
}

/** The voltage a net sits at, from its named supply: a range (equal ends = fixed), or undefined while it is set at runtime. */
export function netVoltage(net: WiringNet, specs: Readonly<Record<string, NetSpec>>): { min: number; max: number } | undefined {
  const supplies = net.named.map((name) => specs[name]).filter((spec): spec is NetSpec => !!spec && spec.kind === 'supply' && spec.mV_min !== undefined && spec.mV_max !== undefined)
  if (net.role !== 'power' || supplies.length !== 1) return undefined
  return { min: supplies[0].mV_min as number, max: supplies[0].mV_max as number }
}
