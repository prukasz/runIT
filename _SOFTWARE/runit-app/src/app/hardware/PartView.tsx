import { ConnectorShape } from './ConnectorShape'
import { connectorPinPoints, leadOut, partTransform, pinRadius, placedSize, takesWires } from './geometry'
import { svgUrl } from './library'
import type { Connector } from './library'
import { BOARD_ITEM, endKey } from './layout'
import type { PinNode, PlacedPart } from './netlist'
import type { HardwareState } from './useHardware'

/*
 * What the Devices canvas draws, in mm: a placed part (its drawing, chips, connectors and pins) and the wires. The canvas
 * (HardwareCanvas.tsx) and its magnifier (PinLoupe.tsx) draw the same scene at their own zoom.
 */

/** A wire's colour from what it carries. */
export const wireTone = (node: PinNode | undefined): string => {
  switch (node?.kind) {
    case 'gnd': return 'is-gnd'
    case 'power': return 'is-power'
    case 'bus': return 'is-bus'
    case 'hbridge':
    case 'motor': return 'is-motor'
    default: return 'is-signal'
  }
}

/** A part on the canvas: its drawing, its connectors, its pins (the hit targets: the whole pin, at least `minHit` mm round). */
export function PartView({ placed, state, zoom, hot, minHit = 0 }: { placed: PlacedPart; state: HardwareState; zoom: number; hot: ReadonlySet<string>; minHit?: number }) {
  const { part } = placed
  const selected = state.selection?.kind === 'item' && state.selection.id === placed.id
  const hasProblem = state.power.problems.some((p) => p.item === placed.id && p.severity === 'error')
  const device = placed.device === undefined ? undefined : state.ctx.devices.find((d) => d.id === placed.device)
  // A wire lead as laid out towards what it is wired to.
  const drawn = (c: Connector) => state.net.leads.get(`${placed.id}:${c.id}`) ?? c
  const size = placedSize(part, placed.rotation)
  return (
    <>
    {device && placed.id !== BOARD_ITEM && <text className="hw-part-label" x={placed.x + size.w / 2} y={placed.y - 1.6} fontSize={Math.max(1.6, 11 / zoom)}>{device.name}</text>}
    <g className={`hw-part ${selected ? 'is-selected' : ''} ${hasProblem ? 'has-problem' : ''}`.trim()} data-item={placed.id} transform={partTransform(part, placed)}>
      <rect className="hw-part-hit" x={0} y={0} width={part.size_mm.w} height={part.size_mm.h} />
      <image href={svgUrl(part.svg)} x={0} y={0} width={part.size_mm.w} height={part.size_mm.h} preserveAspectRatio="none" />
      {part.chips?.map((chip) => {
        const lib = chip.type ? state.library.get(chip.type) : undefined
        return lib
          ? <image key={chip.id} href={svgUrl(lib.svg)} x={chip.at.x} y={chip.at.y} width={chip.size_mm.w} height={chip.size_mm.h} preserveAspectRatio="none" />
          : <g key={chip.id} className="hw-chip"><rect x={chip.at.x} y={chip.at.y} width={chip.size_mm.w} height={chip.size_mm.h} rx={0.4} /><text x={chip.at.x + chip.size_mm.w / 2} y={chip.at.y + chip.size_mm.h / 2} fontSize={Math.min(2.2, chip.size_mm.h / 3)}>{chip.label}</text></g>
      })}
      {part.connectors.map((c) => <ConnectorShape key={c.id} c={drawn(c)} out={leadOut(part, c)} />)}
      <rect className="hw-part-outline" x={-0.6} y={-0.6} width={part.size_mm.w + 1.2} height={part.size_mm.h + 1.2} rx={1} />
      {part.connectors.filter(takesWires).flatMap((c) => connectorPinPoints(drawn(c)).map((p, i) => {
        const pin = c.pins[i]!
        const key = endKey({ item: placed.id, pin: `${c.id}.${pin.id}` })
        const verdict = state.verdicts?.get(key)
        const isPicked = state.picked?.key === key
        const cls = isPicked ? 'is-picked' : verdict ? (verdict.ok ? 'is-ok' : 'is-no') : hot.has(key) ? 'is-wired' : ''
        const title = `${pin.label}${pin.description ? ` (${pin.description})` : ''} · ${c.label ?? c.id}${verdict ? `\n${verdict.ok ? `✓ ${verdict.note ?? 'Can be wired'}` : `✗ ${verdict.why}`}` : ''}`
        return (
          <circle key={key} className={`hw-pin ${cls}`.trim()} data-pin={key} cx={p.x} cy={p.y} r={Math.max(pinRadius(c), minHit)}>
            <title>{title}</title>
          </circle>
        )
      }))}
    </g>
    </>
  )
}

/** The wires between pins, each coloured by what it carries (a lead keeps its own colour). */
export function WireLines({ state, zoom }: { state: HardwareState; zoom: number }) {
  const ends = (key: string) => state.net.nodes.get(key)
  return (
    <>
      {state.layout.wires.map((w) => {
        const [a, b] = [ends(endKey(w.a)), ends(endKey(w.b))]
        if (!a || !b) return null
        const sag = Math.min(18, Math.hypot(b.at.x - a.at.x, b.at.y - a.at.y) * 0.18)
        const [ea, eb] = [a.exit ?? { x: 0, y: 1 }, b.exit ?? { x: 0, y: 1 }]
        const [ka, kb] = [a.exit ? Math.max(sag, 4) : sag, b.exit ? Math.max(sag, 4) : sag]
        const d = `M ${a.at.x} ${a.at.y} C ${a.at.x + ea.x * ka} ${a.at.y + ea.y * ka}, ${b.at.x + eb.x * kb} ${b.at.y + eb.y * kb}, ${b.at.x} ${b.at.y}`
        const lead = a.pin.color ? a : b.pin.color ? b : undefined
        const selected = state.selection?.kind === 'wire' && state.selection.id === w.id
        return (
          <g key={w.id} className={`hw-wire ${wireTone(a.kind === 'power' || a.kind === 'gnd' ? a : b)} ${selected ? 'is-selected' : ''}`.trim()} data-wire={w.id}>
            <path className="hw-wire-hit" d={d} strokeWidth={Math.max(1.4, 10 / zoom)} />
            <path className="hw-wire-outline" d={d} />
            <path className="hw-wire-core" d={d} style={lead?.pin.color ? { stroke: lead.pin.color } : undefined} />
            <title>{`${a.pin.label} (${a.part.name}) — ${b.pin.label} (${b.part.name})`}</title>
          </g>
        )
      })}
    </>
  )
}
