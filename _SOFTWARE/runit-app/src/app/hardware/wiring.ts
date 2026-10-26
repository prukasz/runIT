import { takesWires } from './geometry'
import type { HardwareContext, Netlist, PinNode } from './netlist'
import { consumerField, deviceOf, groupOf, pinTakenBy, providerModes } from './netlist'
import type { PowerResult } from './power'
import { intersect, isEmpty, offerOf, rangeText } from './power'

/*
 * What may be wired to what, pure: the rules behind the highlight (a pin picked on the canvas lights every pin it may go to) and
 * behind a wire being made. Each verdict says why not, or what the wire will do.
 *
 *   ground          to ground
 *   power           a sink to a supply whose voltage fits (a regulator the canvas sets: what it can be set to for all on it),
 *                   or to a net that only passes power on; never two supplies together
 *   device pins     a part's config pin (a servo's PWM) to a free pin of a device whose pins take that mode (PWM)
 *   buses           the same line of the same bus (SDA to SDA)
 *   motors          a motor's two leads to the two outputs of one bridge (OUT1 + OUT2), or one lead to ground (a half bridge)
 */

export interface Scene {
  readonly net: Netlist
  readonly power: PowerResult
  readonly ctx: HardwareContext
}

export type Verdict = { readonly ok: true; readonly note?: string } | { readonly ok: false; readonly why: string }

const no = (why: string): Verdict => ({ ok: false, why })
const yes = (note?: string): Verdict => (note ? { ok: true, note } : { ok: true })

const powerNode = (s: Scene, n: PinNode): string | undefined => (n.pin.net === undefined ? undefined : s.power.nodeOf.get(`${n.end.item}:${n.pin.net}`))

const hasKind = (s: Scene, n: PinNode, kind: PinNode['kind'], except?: string): PinNode | undefined =>
  groupOf(s.net, n.key).find((m) => m.kind === kind && m.key !== except)

/** A provider and a consumer: the consumer's device takes the provider's pin into its config. */
const signal = (s: Scene, consumer: PinNode, provider: PinNode): Verdict => {
  const field = consumerField(s.ctx, consumer)
  if (!field) return no(`${consumer.part.name} has no device yet`)
  if (provider.device === undefined || !deviceOf(s.ctx, provider.device)) return no(`${provider.part.name} has no device yet`)
  const modes = providerModes(s.ctx, provider)
  if (field.requires && !modes.includes(field.requires)) return no(`${field.label} needs a ${field.requires.toUpperCase()} pin; this one is ${modes.join(' / ') || 'no IO pin'}`)
  const taken = pinTakenBy(s.ctx, provider.device, provider.pin.device_pin!.pin, consumer.device)
  if (taken) return no(`Used by ${taken}`)
  if (hasKind(s, provider, 'consumer')) return no('Another part uses this pin')
  if (hasKind(s, consumer, 'provider')) return no(`${field.label} is wired to another pin`)
  return yes(`${field.label} of ${deviceOf(s.ctx, consumer.device)?.name ?? consumer.part.name} becomes this pin`)
}

/** Two power pins. */
const power = (s: Scene, a: PinNode, b: PinNode): Verdict => {
  const [na, nb] = [powerNode(s, a), powerNode(s, b)]
  if (!na || !nb) return no('Not a power pin')
  if (na === nb) return no('Already on the same supply')
  const [pa, pb] = [s.power.nodes.get(na), s.power.nodes.get(nb)]
  // A board's input (an internal net: USB VBUS, the XT60) takes a supply, never a part that draws power.
  const role = (n: PinNode) => n.part.nets?.find((x) => x.id === n.pin.net)?.role
  if ((role(a) === 'internal' && role(b) === 'sink') || (role(b) === 'internal' && role(a) === 'sink')) return no('A power input of the board: a battery or a supply goes here')
  if (pa?.driver && pb?.driver) return no(`Joins two supplies: ${pa.label} and ${pb.label}`)
  const driven = pa?.driver ? pa : pb?.driver ? pb : undefined
  const other = driven === pa ? pb : pa
  if (!driven) return yes()
  const offer = offerOf(s.power, driven.id)
  const want = other?.need
  if (want && isEmpty(intersect(offer, want))) return no(`Needs ${rangeText(want)}; ${driven.label} gives ${rangeText(offer)}`)
  const e = driven.driver ? s.power.elements.get(driven.driver) : undefined
  if (e?.element.kind === 'regulator' && e.element.adjustable && !e.byUser && want) return yes(`${driven.label} is set for it (${rangeText(intersect(offer, want))})`)
  return yes(driven.voltage !== undefined ? `${driven.label}: ${rangeText({ min: driven.voltage, max: driven.voltage })}` : undefined)
}

/** A motor lead and an H-bridge output (or ground). */
const motor = (s: Scene, lead: PinNode, to: PinNode): Verdict => {
  if (hasKind(s, to, 'motor')) return no('Another motor is on this output')
  const other = [...s.net.nodes.values()].find((n) => n.end.item === lead.end.item && n.kind === 'motor' && n.key !== lead.key)
  const otherOut = other && hasKind(s, other, 'hbridge')
  if (to.kind === 'gnd') return otherOut ? yes('Half bridge: the driver needs its half-bridge topology') : no('Wire the other lead to an output first, or this one to an output')
  const hb = to.pin.hbridge!
  if (otherOut) {
    const o = otherOut.pin.hbridge!
    if (o.device !== hb.device || Math.floor(o.channel / 2) !== Math.floor(hb.channel / 2) || o.channel === hb.channel) {
      return no(`The other lead is on ${otherOut.pin.label}: the two go to one bridge (OUT${(Math.floor(o.channel / 2) * 2) + 1} + OUT${(Math.floor(o.channel / 2) * 2) + 2})`)
    }
  }
  const device = deviceOf(s.ctx, hb.device)
  return yes(`${device?.name ?? 'The driver'}, bridge ${hb.channel < 2 ? 'A' : 'B'}`)
}

/** Whether a wire may go from `a` to `b`, and what it does. */
export const canWire = (s: Scene, a: PinNode, b: PinNode): Verdict => {
  if (a.key === b.key) return no('The same pin')
  if (a.end.item === b.end.item) return no("A part's own pins")
  const socket = [a, b].find((n) => !takesWires(n.connector))
  if (socket) return no(`${socket.connector.label ?? socket.connector.id} takes a cable, not a wire`)
  if (s.net.group.get(a.key) === s.net.group.get(b.key)) return no('Already connected')
  const kinds = [a.kind, b.kind]
  if (kinds.includes('nc') || kinds.includes('none')) return no('Not connected inside the part')
  const pair = (x: PinNode['kind'], y: PinNode['kind']) => (a.kind === x && b.kind === y ? [a, b] as const : b.kind === x && a.kind === y ? [b, a] as const : undefined)
  if (a.kind === 'gnd' && b.kind === 'gnd') return yes()
  const motorGnd = pair('motor', 'gnd')
  if (motorGnd) return motor(s, motorGnd[0], motorGnd[1])
  if (kinds.includes('gnd')) return no('Ground goes to ground')
  if (a.kind === 'power' && b.kind === 'power') return power(s, a, b)
  const sig = pair('consumer', 'provider')
  if (sig) return signal(s, sig[0], sig[1])
  if (a.kind === 'bus' && b.kind === 'bus') {
    const [x, y] = [a.pin.bus!, b.pin.bus!]
    if (x.type !== y.type || x.line !== y.line) return no(`${x.type.toUpperCase()} ${x.line.toUpperCase()} goes to ${x.type.toUpperCase()} ${x.line.toUpperCase()}`)
    if (x.bus !== undefined && y.bus !== undefined) return no('Two buses')
    return yes()
  }
  const mot = pair('motor', 'hbridge')
  if (mot) return motor(s, mot[0], mot[1])
  if (a.kind === 'consumer' && b.kind === 'consumer') return no('Two inputs: wire each to a pin of a device')
  if (a.kind === 'provider' && b.kind === 'provider') return no('Two device pins')
  return no(`${a.pin.label} and ${b.pin.label} do not go together`)
}

/** Every pin a picked pin may go to, and why not for the rest. */
export const verdictsFor = (s: Scene, from: PinNode): Map<string, Verdict> => {
  const out = new Map<string, Verdict>()
  for (const n of s.net.nodes.values()) if (n.key !== from.key) out.set(n.key, canWire(s, from, n))
  return out
}
