import { endpointKey, flowDirection, specOf } from '../../domain/hardware'
import type { Endpoint, Wire } from '../../domain/hardware'
import type { BoardWiring } from '../useBoardWiring'
import type { EndpointPositions } from './endpoints'
import { netLabel } from './wiringReport'
import type { WiringReport } from './wiringReport'

/*
 * The wires over the board canvas: an SVG in the canvas's world pixels, so it pans and zooms with the art. A wire is a
 * curve between two pin positions measured from the DOM; its colour is its net's role (power, ground, bus, signal),
 * dashed red when the checks found an error on it. Stroke widths and dots keep their size on screen at any zoom.
 */

type Point = { readonly x: number; readonly y: number }

/** The control points of the curve between two pins: it leaves each pin sideways, so wires fan out instead of cutting across. */
const curve = (a: Point, b: Point): [Point, Point, Point, Point] => {
  const reach = Math.min(160, Math.max(24, Math.abs(b.x - a.x) / 2, Math.abs(b.y - a.y) / 4))
  return [a, { x: a.x + reach, y: a.y }, { x: b.x - reach, y: b.y }, b]
}

export const wirePath = (a: Point, b: Point): string => {
  const [p0, p1, p2, p3] = curve(a, b)
  return `M ${p0.x} ${p0.y} C ${p1.x} ${p1.y}, ${p2.x} ${p2.y}, ${p3.x} ${p3.y}`
}

/** Points along the curve, for picking a wire and placing its label. */
export const wirePoints = (a: Point, b: Point, steps = 24): Point[] => {
  const [p0, p1, p2, p3] = curve(a, b)
  return Array.from({ length: steps + 1 }, (_, index) => {
    const t = index / steps
    const u = 1 - t
    return {
      x: u * u * u * p0.x + 3 * u * u * t * p1.x + 3 * u * t * t * p2.x + t * t * t * p3.x,
      y: u * u * u * p0.y + 3 * u * u * t * p1.y + 3 * u * t * t * p2.y + t * t * t * p3.y,
    }
  })
}

/** Distance from a point to a polyline. */
export function distanceToPolyline(point: Point, line: readonly Point[]): number {
  let best = Infinity
  for (let i = 1; i < line.length; i++) {
    const a = line[i - 1]
    const b = line[i]
    const dx = b.x - a.x
    const dy = b.y - a.y
    const length = dx * dx + dy * dy
    const t = length ? Math.max(0, Math.min(1, ((point.x - a.x) * dx + (point.y - a.y) * dy) / length)) : 0
    best = Math.min(best, Math.hypot(point.x - (a.x + t * dx), point.y - (a.y + t * dy)))
  }
  return best
}

export interface PendingWire {
  readonly from: Endpoint
  /** The pointer, in world pixels. */
  readonly x: number
  readonly y: number
  readonly target?: Endpoint
  /** Why the pin under the pointer cannot take the wire. */
  readonly problem?: string
}

export function BoardWires({ wires, positions, report, wiring, selectedDevice, pending, fits, zoom }: {
  wires: readonly Wire[]
  positions: EndpointPositions
  report: WiringReport
  wiring: BoardWiring
  selectedDevice?: string
  pending?: PendingWire
  /** While a wire is dragged: where the pins that can take it are. */
  fits: EndpointPositions
  zoom: number
}) {
  const unit = 1 / zoom
  return (
    <svg className="board-wires" width="1" height="1" aria-hidden="true">
      {wires.map((wire) => {
        const a = positions.get(endpointKey(wire.a))
        const b = positions.get(endpointKey(wire.b))
        if (!a || !b) return null
        const net = report.netOfWire.get(wire.id)
        const role = net?.role ?? 'signal'
        const issues = report.issuesOfWire.get(wire.id) ?? []
        const faded =
          ((role === 'power' || role === 'ground') && !wiring.power) ||
          ((role === 'bus' || role === 'signal') && !wiring.io) ||
          (wiring.focus && !!selectedDevice && wire.a.device !== selectedDevice && wire.b.device !== selectedDevice)
        const label = wiring.voltages && net ? netLabel(net, report) : undefined
        const middle = wirePoints(a, b)[12]
        const bad = issues.some((issue) => issue.severity === 'error')
        // An arrow from the producer to the consumer, when the roles tell which is which.
        const specA = specOf(report.context, wire.a)
        const specB = specOf(report.context, wire.b)
        const direction = specA && specB ? flowDirection(specA, specB) : undefined
        const points = wirePoints(a, b)
        const from = points[direction === 'ba' ? 14 : 10]
        const to = points[direction === 'ba' ? 10 : 14]
        const heading = Math.atan2(to.y - from.y, to.x - from.x)
        const classes = ['board-wire', `is-${role}`, wiring.selectedWire === wire.id ? 'is-selected' : '', bad ? 'has-error' : issues.some((issue) => issue.severity === 'warning') ? 'has-warning' : '', faded ? 'is-faded' : ''].filter(Boolean).join(' ')
        return (
          <g key={wire.id} className={classes} data-wire={wire.id}>
            <path className="board-wire-halo" d={wirePath(a, b)} />
            <path className="board-wire-line" d={wirePath(a, b)} />
            <circle className="board-wire-end" cx={a.x} cy={a.y} r={4 * unit} />
            <circle className="board-wire-end" cx={b.x} cy={b.y} r={4 * unit} />
            {direction && <polygon className="board-wire-arrow" points={`${7 * unit},0 ${-5 * unit},${-5 * unit} ${-5 * unit},${5 * unit}`} transform={`translate(${middle.x} ${middle.y}) rotate(${(heading * 180) / Math.PI})`} />}
            {label && (
              <text className={`board-wire-label${issues.some((issue) => issue.code === 'overvoltage' || issue.code === 'undervoltage') ? ' is-bad' : ''}`} x={middle.x} y={middle.y - 6 * unit} fontSize={11 * unit} textAnchor="middle">{label}</text>
            )}
          </g>
        )
      })}

      {[...fits].map(([key, at]) => <circle key={key} className="board-wire-fit" cx={at.x} cy={at.y} r={8 * unit} />)}

      {pending && positions.get(endpointKey(pending.from)) && (
        <g className={`board-wire is-pending${pending.problem ? ' is-refused' : ''}`}>
          {(() => {
            const start = positions.get(endpointKey(pending.from)) as Point
            const end = pending.target && !pending.problem ? (positions.get(endpointKey(pending.target)) ?? fits.get(endpointKey(pending.target)) ?? pending) : pending
            return <path className="board-wire-line" d={wirePath(start, end)} />
          })()}
        </g>
      )}
    </svg>
  )
}
