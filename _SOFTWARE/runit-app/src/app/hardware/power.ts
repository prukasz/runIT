import type { PartNet, PowerElement, Range } from './library'
import { elementInputs } from './library'
import { BOARD_ITEM } from './layout'
import type { PowerChoices } from './layout'
import type { Netlist, PlacedPart } from './netlist'

/*
 * The flow of energy on the canvas, pure. A power node is a set of part nets joined by wires (a servo's VCC, the board's Rail B
 * pins, a breakout's V+ between them); a motor lead on an H-bridge output joins the motor's net to the bridge's supply. The
 * elements of the parts (the board's sources, eFuses, regulators, muxes, its own loads) feed nodes from nodes.
 *
 *   1. What each node needs: the voltage ranges of the sinks on it, carried up through the muxes and the fuses that feed it.
 *   2. The choices: the source and its voltage (a USB PD level: the lowest that fits and carries the load), each mux's input
 *      (the one whose voltage fits; a rail that can be set to fit before an input whose voltage is not known), each adjustable
 *      regulator's voltage (5 V when every sink on it takes 5 V, else the lowest wanted voltage, `nominal`, of its sinks that
 *      every sink accepts). A user's choice wins.
 *   3. The voltage of every node, from the source down, and the current: the sinks' typical and peak draw, carried back up to
 *      the source (a regulator's input current from its output power and efficiency).
 *   4. What is wrong: a sink with no supply or the wrong voltage, a node fed twice, a current over a limit.
 */

export interface PowerSink {
  readonly item: string
  readonly net: PartNet
}

export interface PowerNode {
  /** `item:net` of its first net. */
  readonly id: string
  readonly nets: readonly { readonly item: string; readonly net: PartNet }[]
  readonly label: string
  readonly sinks: readonly PowerSink[]
  /** What its sinks accept, together (and what the nodes it feeds through muxes and fuses need). Undefined: no sink. */
  readonly need?: Range
  /** The voltage it has; undefined: not powered (or not known: an external supply whose voltage is not set). */
  readonly voltage?: number
  /** Typical and peak current drawn from it, mA. */
  readonly current: { readonly typical: number; readonly peak: number }
  /** The element that feeds it. */
  readonly driver?: string
}

export interface ElementState {
  readonly element: PowerElement
  readonly item: string
  readonly active: boolean
  readonly in_mV?: number
  readonly out_mV?: number
  readonly current: { readonly typical: number; readonly peak: number }
  /** A mux: the input it takes. */
  readonly input?: number
  /** A regulator or a mux whose setting the canvas made: what on it asked for it. */
  readonly lockedBy?: readonly PowerSink[]
  /** The choice is the user's (not picked by the canvas). */
  readonly byUser?: boolean
  /** An adjustable regulator: the voltage it is set to (also while its input has none). */
  readonly setpoint_mV?: number
}

export type PowerSeverity = 'error' | 'warning' | 'info'

export interface PowerProblem {
  readonly severity: PowerSeverity
  readonly text: string
  /** What it is about: a node, an element (`item:element`), or a part. */
  readonly node?: string
  readonly element?: string
  readonly item?: string
}

export interface PowerResult {
  readonly nodes: ReadonlyMap<string, PowerNode>
  /** The node of each part net (`item:net`). */
  readonly nodeOf: ReadonlyMap<string, string>
  readonly elements: ReadonlyMap<string, ElementState>
  /** The source that feeds the board and its voltage. */
  readonly source?: { readonly element: string; readonly mV?: number }
  readonly problems: readonly PowerProblem[]
}

const netKey = (item: string, net: string): string => `${item}:${net}`

/** What an adjustable regulator is set to when everything on it takes it: 5 V, the hobby default. */
export const DEFAULT_RAIL_MV = 5000

export const intersect = (a: Range | undefined, b: Range | undefined): Range | undefined => {
  if (!a) return b
  if (!b) return a
  const min = Math.max(a.min ?? -Infinity, b.min ?? -Infinity)
  const max = Math.min(a.max ?? Infinity, b.max ?? Infinity)
  return { ...(Number.isFinite(min) ? { min } : {}), ...(Number.isFinite(max) ? { max } : {}) }
}
export const isEmpty = (r: Range | undefined): boolean => r !== undefined && r.min !== undefined && r.max !== undefined && r.min > r.max
export const within = (v: number, r: Range | undefined): boolean => !r || ((r.min === undefined || v >= r.min) && (r.max === undefined || v <= r.max))

export const volts = (mV: number | undefined): string => (mV === undefined ? '?' : `${(mV / 1000).toFixed(mV % 1000 === 0 ? 0 : mV % 100 === 0 ? 1 : 2)} V`)
export const rangeText = (r: Range | undefined): string =>
  !r ? 'any' : r.min !== undefined && r.max !== undefined ? (r.min === r.max ? volts(r.min) : `${volts(r.min)}–${volts(r.max)}`) : r.min !== undefined ? `≥ ${volts(r.min)}` : r.max !== undefined ? `≤ ${volts(r.max)}` : 'any'
export const amps = (mA: number): string => (mA >= 1000 ? `${(mA / 1000).toFixed(2)} A` : `${Math.round(mA)} mA`)

/** Which part net a motor draws through: its sink net. */
const motorNet = (placed: PlacedPart): PartNet | undefined => placed.part.nets?.find((n) => n.role === 'sink')

export const solvePower = (net: Netlist, choices: PowerChoices = {}): PowerResult => {
  const placed = new Map(net.parts.map((p) => [p.id, p]))
  // 1. Nodes: the part nets, joined through the wires (and the motors' leads to their bridge's supply).
  const parent = new Map<string, string>()
  const netInfo = new Map<string, { item: string; net: PartNet }>()
  for (const p of net.parts) for (const n of p.part.nets ?? []) {
    const k = netKey(p.id, n.id)
    parent.set(k, k)
    netInfo.set(k, { item: p.id, net: n })
  }
  const find = (k: string): string => {
    let r = k
    while (parent.get(r) !== r) r = parent.get(r)!
    parent.set(k, r)
    return r
  }
  const join = (a: string, b: string) => {
    if (!parent.has(a) || !parent.has(b)) return
    const [ra, rb] = [find(a), find(b)]
    if (ra !== rb) parent.set(rb, ra)
  }
  for (const keys of net.members.values()) {
    let first: string | undefined
    for (const k of keys) {
      const node = net.nodes.get(k)!
      let pk: string | undefined
      if (node.pin.net !== undefined) pk = netKey(node.end.item, node.pin.net)
      if (node.pin.hbridge?.supply) pk = netKey(node.end.item, node.pin.hbridge.supply)
      if (node.pin.motor) {
        const m = placed.get(node.end.item)
        const sink = m && motorNet(m)
        // A motor lead joins its draw to a bridge's supply only through a bridge output.
        if (sink && keys.some((o) => net.nodes.get(o)?.pin.hbridge)) pk = netKey(node.end.item, sink.id)
      }
      if (pk === undefined) continue
      if (first === undefined) first = pk
      else join(first, pk)
    }
  }
  const nodeOf = new Map<string, string>()
  const groups = new Map<string, string[]>()
  for (const k of parent.keys()) {
    const r = find(k)
    nodeOf.set(k, r)
    groups.set(r, [...(groups.get(r) ?? []), k])
  }

  // Elements by item, keyed `item:element`.
  const elements = new Map<string, { item: string; element: PowerElement }>()
  for (const p of net.parts) for (const e of p.part.power?.elements ?? []) elements.set(`${p.id}:${e.id}`, { item: p.id, element: e })
  const nodeAt = (item: string, netId: string | undefined): string | undefined => (netId === undefined ? undefined : nodeOf.get(netKey(item, netId)))
  const feeds = new Map<string, string[]>() // node -> elements whose out is it
  for (const [key, { item, element }] of elements) {
    const out = nodeAt(item, element.out)
    if (out) feeds.set(out, [...(feeds.get(out) ?? []), key])
  }
  const sinksOf = (node: string): PowerSink[] =>
    (groups.get(node) ?? []).map((k) => netInfo.get(k)!).filter((n) => n.net.role === 'sink').map(({ item, net: n }) => ({ item, net: n }))
  // An external source: a part's source net that no element of its part feeds (a battery).
  const externalOf = (node: string): { item: string; net: PartNet }[] =>
    (groups.get(node) ?? []).map((k) => netInfo.get(k)!).filter(({ item, net: n }) => n.role === 'source' &&
      ![...elements.values()].some((e) => e.item === item && e.element.out === n.id))

  // 2a. What each node needs: its sinks, then (below) what the muxes and fuses it feeds need.
  const need = new Map<string, Range | undefined>()
  for (const node of groups.keys()) {
    let r: Range | undefined
    for (const s of sinksOf(node)) r = intersect(r, s.net.voltage_mV ? { min: s.net.voltage_mV.min, max: s.net.voltage_mV.max } : undefined)
    need.set(node, r)
  }

  // The source: the user's, an element fed by an external part, else the first.
  const sourceKeys = [...elements].filter(([, e]) => e.element.kind === 'source').map(([k]) => k)
  const fedExternally = sourceKeys.find((k) => {
    const e = elements.get(k)!
    const out = nodeAt(e.item, e.element.out)
    return out !== undefined && externalOf(out).length > 0
  })
  const sourceKey = (choices.source && sourceKeys.find((k) => k.endsWith(`:${choices.source}`))) || fedExternally || sourceKeys[0]

  const problems: PowerProblem[] = []
  const muxInput = new Map<string, number>()
  const muxByUser = new Set<string>()
  const setpoint = new Map<string, number>()
  const setByUser = new Set<string>()
  let source_mV: number | undefined

  /** The voltage a node can have before the choices: what feeds it can give. */
  const possible = (node: string, depth = 0): { range?: Range; known: boolean; adjustable: boolean; levels?: readonly number[] } => {
    if (depth > 12) return { known: false, adjustable: false }
    const ext = externalOf(node)[0]
    if (ext?.net.voltage_mV) return { range: ext.net.voltage_mV, known: ext.net.voltage_mV.nominal !== undefined, adjustable: false }
    const driver = (feeds.get(node) ?? []).map((k) => elements.get(k)!)
    for (const { item, element } of driver) {
      if (element.kind === 'source') {
        if (`${item}:${element.id}` !== sourceKey) continue
        if (choices.input_mV !== undefined) return { range: { min: choices.input_mV, max: choices.input_mV }, known: true, adjustable: false }
        return element.levels_mV ? { range: element.voltage_mV, known: false, adjustable: true, levels: element.levels_mV } : { range: element.voltage_mV, known: false, adjustable: false }
      }
      if (element.kind === 'regulator') {
        const fixed = element.voltage_mV && element.voltage_mV.min === element.voltage_mV.max
        return { range: element.voltage_mV, known: !!fixed, adjustable: !!element.adjustable }
      }
    }
    // Fuses and muxes pass on what their (active) input has.
    for (const { item, element } of driver) {
      if (element.kind !== 'fuse') continue
      const input = nodeAt(item, elementInputs(element)[0])
      if (!input) continue
      const p = possible(input, depth + 1)
      if (p.range || p.levels) return p
    }
    return { known: false, adjustable: false }
  }

  // 2b. The muxes: the input whose voltage fits what the output feeds. Then what each needs goes up to its input.
  const order = [...elements].filter(([, e]) => e.element.kind === 'mux')
  for (const [key, { item, element }] of order) {
    const out = nodeAt(item, element.out)
    const ins = elementInputs(element).map((n) => nodeAt(item, n))
    const wanted = out ? need.get(out) : undefined
    const user = choices.muxes?.[element.id]
    if (user !== undefined && user >= 0 && user < ins.length) {
      muxInput.set(key, user)
      muxByUser.add(key)
    } else if (out && sinksOf(out).length > 0) {
      const options = ins.map((n, i) => ({ i, p: n ? possible(n) : { known: false, adjustable: false } }))
      const fits = (p: ReturnType<typeof possible>) => p.range !== undefined && !isEmpty(intersect(p.range, wanted))
      const pick = options.find((o) => o.p.known && fits(o.p) && within(o.p.range!.min!, wanted))
        ?? options.find((o) => o.p.adjustable && fits(o.p) && !isEmpty(intersect(intersect(o.p.range, wanted), need.get(ins[o.i]!))))
        ?? options.find((o) => fits(o.p))
      muxInput.set(key, pick?.i ?? 0)
    }
    const chosen = muxInput.get(key)
    const input = chosen === undefined ? undefined : ins[chosen]
    if (input && wanted) need.set(input, intersect(need.get(input), wanted))
  }
  // Fuses: what their output needs, their input needs.
  for (const [, { item, element }] of elements) {
    if (element.kind !== 'fuse') continue
    const out = nodeAt(item, element.out)
    const input = nodeAt(item, elementInputs(element)[0])
    if (out && input && need.get(out)) need.set(input, intersect(need.get(input), need.get(out)))
  }

  // 2c. Adjustable regulators: the user's voltage, else the lowest nominal its sinks want that all accept.
  const lockedBy = new Map<string, PowerSink[]>()
  const downstreamSinks = (node: string, seen = new Set<string>()): PowerSink[] => {
    if (seen.has(node)) return []
    seen.add(node)
    const own = sinksOf(node)
    const viaMux = [...elements].filter(([k, e]) => e.element.kind === 'mux' && elementInputs(e.element).some((n, i) => nodeAt(e.item, n) === node && muxInput.get(k) === i))
      .flatMap(([, e]) => { const out = nodeAt(e.item, e.element.out); return out ? downstreamSinks(out, seen) : [] })
    return [...own, ...viaMux]
  }
  for (const [key, { item, element }] of elements) {
    if (element.kind !== 'regulator' || !element.adjustable) continue
    const out = nodeAt(item, element.out)
    if (!out) continue
    const sinks = downstreamSinks(out)
    const wanted = intersect(need.get(out), element.voltage_mV)
    const user = choices.rails?.[element.id]
    if (user !== undefined) {
      setpoint.set(key, user)
      setByUser.add(key)
      continue
    }
    if (sinks.length === 0) continue
    lockedBy.set(key, sinks)
    if (isEmpty(wanted)) {
      problems.push({ severity: 'error', element: key, text: `${element.label}: what is on it wants ${sinks.map((s) => rangeText(s.net.voltage_mV)).join(' and ')}, there is no voltage for all` })
      continue
    }
    const step = element.voltage_mV?.step ?? 100
    const nominals = sinks.map((s) => s.net.voltage_mV?.nominal).filter((v): v is number => v !== undefined && within(v, wanted))
    const raw = within(DEFAULT_RAIL_MV, wanted) ? DEFAULT_RAIL_MV : nominals.length > 0 ? Math.min(...nominals) : (wanted?.min ?? element.voltage_mV?.min ?? DEFAULT_RAIL_MV)
    const snapped = Math.ceil(raw / step) * step
    setpoint.set(key, within(snapped, wanted) ? snapped : raw)
  }

  // 2d. The source's voltage: the user's, an external part's, else the lowest USB PD level that fits (and carries the load: 3.).
  const sourceEntry = sourceKey ? elements.get(sourceKey) : undefined
  const sourceOut = sourceEntry && nodeAt(sourceEntry.item, sourceEntry.element.out)
  const externalPart = sourceOut ? externalOf(sourceOut)[0] : undefined
  const sourceNeed = sourceOut ? need.get(sourceOut) : undefined
  const levels = sourceEntry?.element.levels_mV?.filter((l) => within(l, sourceNeed)) ?? []

  // 3. Voltages and currents for a source voltage.
  const evaluate = (mV: number | undefined) => {
    const voltage = new Map<string, number>()
    const active = new Map<string, { in_mV?: number; out_mV?: number }>()
    const driverOf = new Map<string, string>()
    const drivenTwice = new Set<string>()
    const set = (node: string | undefined, v: number | undefined, by: string) => {
      if (!node || v === undefined) return
      if (driverOf.has(node) && driverOf.get(node) !== by) drivenTwice.add(node)
      driverOf.set(node, by)
      voltage.set(node, v)
    }
    if (sourceEntry && sourceOut) {
      set(sourceOut, mV, sourceKey!)
      active.set(sourceKey!, { out_mV: mV })
    }
    for (const node of groups.keys()) {
      const ext = externalOf(node)[0]
      if (ext && !(sourceOut === node)) set(node, ext.net.voltage_mV?.nominal ?? ext.net.voltage_mV?.max, `${ext.item}:${ext.net.id}`)
    }
    // Feed forward until nothing changes (the elements form a short chain).
    for (let pass = 0; pass < 8; pass++) {
      for (const [key, { item, element }] of elements) {
        if (element.kind === 'source' || element.kind === 'load' || active.has(key)) continue
        const ins = elementInputs(element).map((n) => nodeAt(item, n))
        const out = nodeAt(item, element.out)
        if (element.kind === 'fuse') {
          const v = ins[0] === undefined ? undefined : voltage.get(ins[0])
          if (v !== undefined && v !== 0) { active.set(key, { in_mV: v, out_mV: v }); set(out, v, key) }
        } else if (element.kind === 'regulator') {
          const v = ins[0] === undefined ? undefined : voltage.get(ins[0])
          if (v === undefined) continue
          const fixed = element.voltage_mV && element.voltage_mV.min === element.voltage_mV.max ? element.voltage_mV.min : undefined
          const o = element.adjustable ? setpoint.get(key) : fixed
          if (o === undefined) continue
          active.set(key, { in_mV: v, out_mV: o })
          set(out, o, key)
        } else if (element.kind === 'mux') {
          const i = muxInput.get(key)
          const at = i === undefined ? undefined : ins[i]
          const v = at === undefined ? undefined : voltage.get(at)
          if (v === undefined) continue
          active.set(key, { in_mV: v, out_mV: v })
          set(out, v, key)
        }
      }
    }
    for (const [key, { item, element }] of elements) if (element.kind === 'load') {
      const v = voltage.get(nodeAt(item, elementInputs(element)[0]) ?? '')
      if (v !== undefined) active.set(key, { in_mV: v })
    }
    // Currents: the sinks and the loads of a node, and what the elements it feeds draw from it.
    const draw = new Map<string, { typical: number; peak: number }>()
    const through = new Map<string, { typical: number; peak: number }>()
    const demand = (node: string, depth = 0): { typical: number; peak: number } => {
      const known = draw.get(node)
      if (known || depth > 16) return known ?? { typical: 0, peak: 0 }
      let typical = 0
      let peak = 0
      for (const s of sinksOf(node)) { typical += s.net.current_mA?.typical ?? 0; peak += s.net.current_mA?.peak ?? s.net.current_mA?.typical ?? 0 }
      for (const [key, { item, element }] of elements) {
        if (!active.has(key)) continue
        const ins = elementInputs(element).map((n) => nodeAt(item, n))
        const idx = ins.indexOf(node)
        if (idx < 0) continue
        let c = { typical: 0, peak: 0 }
        if (element.kind === 'load') c = { typical: element.current_mA?.typical ?? 0, peak: element.current_mA?.peak ?? element.current_mA?.typical ?? 0 }
        else if (element.kind === 'fuse') c = demand(nodeAt(item, element.out)!, depth + 1)
        else if (element.kind === 'mux') { if (muxInput.get(key) === idx) c = demand(nodeAt(item, element.out)!, depth + 1) }
        else if (element.kind === 'regulator') {
          const o = demand(nodeAt(item, element.out)!, depth + 1)
          const a = active.get(key)!
          const k = (a.out_mV ?? 0) / ((a.in_mV || 1) * ((element.efficiency_pct ?? 100) / 100))
          c = { typical: o.typical * k, peak: o.peak * k }
          through.set(key, o)
        }
        if (element.kind !== 'regulator') through.set(key, c)
        typical += c.typical
        peak += c.peak
      }
      const out = { typical, peak }
      draw.set(node, out)
      return out
    }
    for (const node of groups.keys()) demand(node)
    return { voltage, active, driverOf, drivenTwice, draw, through }
  }

  let chosen_mV: number | undefined = choices.input_mV ?? externalPart?.net.voltage_mV?.nominal
  if (chosen_mV === undefined && levels.length > 0) {
    chosen_mV = levels[0]
    for (const level of levels) {
      const trial = evaluate(level)
      const c = sourceOut ? trial.draw.get(sourceOut) : undefined
      chosen_mV = level
      if (!c || c.peak <= (sourceEntry!.element.max_mA ?? Infinity)) break
    }
  }
  if (sourceEntry?.element.levels_mV && chosen_mV === undefined && levels.length === 0 && sourceNeed) {
    problems.push({ severity: 'error', element: sourceKey, text: `${sourceEntry.element.label}: no USB PD level is ${rangeText(sourceNeed)}` })
  }
  source_mV = chosen_mV
  const result = evaluate(source_mV)

  // 4. What is wrong.
  if (sourceEntry && source_mV === undefined && !sourceEntry.element.levels_mV) {
    problems.push({ severity: 'warning', element: sourceKey, text: `${sourceEntry.element.label}: its voltage is not set; what runs straight from the input is not checked` })
  }
  if (sourceEntry && source_mV !== undefined && !within(source_mV, sourceNeed)) {
    problems.push({ severity: 'error', element: sourceKey, text: `${sourceEntry.element.label} gives ${volts(source_mV)}; what runs from the input wants ${rangeText(sourceNeed)}` })
  }
  for (const node of result.drivenTwice) problems.push({ severity: 'error', node, text: `${labelOf(node)} is fed from two supplies: a wire joins two outputs` })
  const nodes = new Map<string, PowerNode>()
  function labelOf(node: string): string {
    const nets = (groups.get(node) ?? []).map((k) => netInfo.get(k)!)
    return (nets.find((n) => n.item === BOARD_ITEM) ?? nets.find((n) => n.net.role !== 'sink') ?? nets[0])?.net.label ?? node
  }
  for (const [node, keys] of groups) {
    const v = result.voltage.get(node)
    const sinks = sinksOf(node)
    for (const s of sinks) {
      const name = placed.get(s.item)?.part.name ?? s.item
      if (v === undefined) {
        const viaBoard = keys.some((k) => netInfo.get(k)!.item !== s.item)
        problems.push({ severity: viaBoard ? 'warning' : 'error', node, item: s.item, text: viaBoard ? `${name} (${s.net.label}): its supply is off or not set` : `${name} (${s.net.label}) is not connected to a supply` })
      } else if (!within(v, s.net.voltage_mV)) {
        problems.push({ severity: 'error', node, item: s.item, text: `${name} (${s.net.label}) wants ${rangeText(s.net.voltage_mV)}, gets ${volts(v)}` })
      }
    }
    const current = result.draw.get(node) ?? { typical: 0, peak: 0 }
    for (const k of keys) {
      const { item, net: n } = netInfo.get(k)!
      if (n.max_mA !== undefined && n.role !== 'sink' && current.peak > n.max_mA) {
        problems.push({ severity: current.typical > n.max_mA ? 'error' : 'warning', node, text: `${n.label} (${placed.get(item)?.part.name ?? item}): up to ${amps(current.peak)}, it carries ${amps(n.max_mA)}` })
      }
    }
    nodes.set(node, { id: node, nets: keys.map((k) => netInfo.get(k)!), label: labelOf(node), sinks, ...(need.get(node) ? { need: need.get(node)! } : {}),
      ...(v !== undefined ? { voltage: v } : {}), current, ...(result.driverOf.get(node) ? { driver: result.driverOf.get(node)! } : {}) })
  }
  const states = new Map<string, ElementState>()
  for (const [key, { item, element }] of elements) {
    const a = result.active.get(key)
    const ins = elementInputs(element).map((n) => nodeAt(item, n))
    const fromIn = ins.map((n) => (n ? result.draw.get(n) : undefined))
    const out = nodeAt(item, element.out)
    const current = element.kind === 'load' ? { typical: element.current_mA?.typical ?? 0, peak: element.current_mA?.peak ?? 0 }
      : out ? (element.kind === 'regulator' || element.kind === 'mux' || element.kind === 'fuse' ? (result.through.get(key) ?? { typical: 0, peak: 0 }) : (result.draw.get(out) ?? { typical: 0, peak: 0 }))
      : (fromIn[0] ?? { typical: 0, peak: 0 })
    const state: ElementState = {
      element, item, active: a !== undefined, ...(a?.in_mV !== undefined ? { in_mV: a.in_mV } : {}), ...(a?.out_mV !== undefined ? { out_mV: a.out_mV } : {}), current: a ? current : { typical: 0, peak: 0 },
      ...(muxInput.has(key) ? { input: muxInput.get(key)! } : {}),
      ...(lockedBy.has(key) && !setByUser.has(key) ? { lockedBy: lockedBy.get(key)! } : {}),
      ...(element.kind === 'mux' && muxInput.has(key) && !muxByUser.has(key) && out ? { lockedBy: downstreamSinks(out) } : {}),
      ...(setByUser.has(key) || muxByUser.has(key) ? { byUser: true } : {}),
      ...(setpoint.has(key) ? { setpoint_mV: setpoint.get(key)! } : {}),
    }
    states.set(key, state)
    const limit = element.max_mA
    if (a && limit !== undefined && state.current.peak > limit) {
      problems.push({ severity: state.current.typical > limit ? 'error' : 'warning', element: key, text: `${element.label}: up to ${amps(state.current.peak)} (${amps(state.current.typical)} typical), its limit is ${amps(limit)}` })
    }
  }
  const board = placed.get(BOARD_ITEM)
  const limit = board?.part.power?.limits?.max_mA
  if (limit !== undefined && sourceOut) {
    const c = result.draw.get(sourceOut)
    if (c && c.peak > limit) problems.push({ severity: c.typical > limit ? 'error' : 'warning', element: sourceKey, text: `The board's input: up to ${amps(c.peak)}, its limit is ${amps(limit)}` })
  }
  return { nodes, nodeOf, elements: states, ...(sourceKey ? { source: { element: sourceKey, ...(source_mV !== undefined ? { mV: source_mV } : {}) } } : {}), problems }
}

/** The way energy comes to a node: the node, the element that feeds it, its input node, ... up to the source. */
export const pathTo = (power: PowerResult, node: string): { node: PowerNode; element?: ElementState }[] => {
  const out: { node: PowerNode; element?: ElementState }[] = []
  const seen = new Set<string>()
  let at: string | undefined = node
  while (at && !seen.has(at)) {
    seen.add(at)
    const n = power.nodes.get(at)
    if (!n) break
    const e = n.driver ? power.elements.get(n.driver) : undefined
    out.push({ node: n, ...(e ? { element: e } : {}) })
    if (!e) break
    const ins = elementInputs(e.element)
    const pick = e.element.kind === 'mux' ? ins[e.input ?? 0] : ins[0]
    at = pick === undefined ? undefined : power.nodeOf.get(`${e.item}:${pick}`)
  }
  return out
}

/** What a node can give a new sink: its voltage when it is set by a fixed source, else what its regulator or source can be set to. */
export const offerOf = (power: PowerResult, node: string): Range | undefined => {
  const n = power.nodes.get(node)
  if (!n?.driver) return undefined
  const e = power.elements.get(n.driver)
  if (!e) return undefined
  if (e.element.kind === 'regulator' && e.element.adjustable && !e.byUser) return intersect(e.element.voltage_mV, n.need)
  if (e.element.kind === 'source' && e.element.levels_mV && power.source?.mV !== undefined) return intersect(e.element.voltage_mV, n.need)
  return n.voltage !== undefined ? { min: n.voltage, max: n.voltage } : e.element.voltage_mV
}
