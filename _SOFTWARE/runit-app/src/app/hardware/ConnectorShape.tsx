import { connectorBody, connectorPinPoints, turnBy } from './geometry'
import type { DrawnConnector } from './geometry'
import type { Point } from './library'

/** A connector drawn at its true size, in its part's millimetres (a wire lead as laid out, at any angle; `out`: which way it leaves the part). */
export function ConnectorShape({ c, out }: { c: DrawnConnector; out?: Point | undefined }) {
  const points = connectorPinPoints(c)
  const body = connectorBody(c)
  const rotate = `rotate(${c.rotation ?? 0} ${c.at.x} ${c.at.y})`
  const rect = <rect x={c.at.x + body.x0} y={c.at.y + body.y0} width={body.x1 - body.x0} height={body.y1 - body.y0} transform={rotate} />
  switch (c.type) {
    case 'goldpin':
      return <g className="hw-goldpin">{points.map((p, i) => <g key={i}><rect className="hw-goldpin-base" x={p.x - 1.27} y={p.y - 1.27} width={2.54} height={2.54} /><rect className="hw-goldpin-pin" x={p.x - 0.32} y={p.y - 0.32} width={0.64} height={0.64} /></g>)}</g>
    case 'goldpin_female':
      return <g className="hw-goldpin is-female">{points.map((p, i) => <g key={i}><rect className="hw-goldpin-base" x={p.x - 1.27} y={p.y - 1.27} width={2.54} height={2.54} /><rect className="hw-goldpin-hole" x={p.x - 0.45} y={p.y - 0.45} width={0.9} height={0.9} /></g>)}</g>
    case 'screw_terminal': {
      const r = (c.pitch_mm ?? 5) >= 5 ? 1.7 : 1.25
      return (
        <g className="hw-screw">
          {rect}
          {points.map((p, i) => <g key={i}><circle className="hw-screw-head" cx={p.x} cy={p.y} r={r} /><line className="hw-screw-slot" x1={p.x - r * 0.65} y1={p.y + r * 0.25} x2={p.x + r * 0.65} y2={p.y - r * 0.25} /></g>)}
        </g>
      )
    }
    case 'xt60': {
      // The housing's corners are cut on its + side (the pin on a power net), as on the real plug.
      const plusLast = c.pins.findIndex((p) => p.net !== undefined) === c.pins.length - 1
      const [x0, x1, y0, y1] = [c.at.x + body.x0, c.at.x + body.x1, c.at.y + body.y0, c.at.y + body.y1]
      const k = 2.4
      const outline = plusLast
        ? `M ${x0} ${y0} L ${x1 - k} ${y0} L ${x1} ${y0 + k} L ${x1} ${y1 - k} L ${x1 - k} ${y1} L ${x0} ${y1} Z`
        : `M ${x0 + k} ${y0} L ${x1} ${y0} L ${x1} ${y1} L ${x0 + k} ${y1} L ${x0} ${y1 - k} L ${x0} ${y0 + k} Z`
      return <g className="hw-xt60"><path d={outline} transform={rotate} />{points.map((p, i) => <circle key={i} className="hw-xt60-pin" cx={p.x} cy={p.y} r={1.75} />)}</g>
    }
    case 'usb_c':
      return <g className="hw-usbc">{rect}</g>
    case 'pad':
      return <g className="hw-pad">{points.map((p, i) => <rect key={i} x={p.x - 0.5} y={p.y - 0.5} width={1} height={1} />)}</g>
    case 'wire': {
      // Each wire leaves the part side by side with the others, straight out of its side, and goes into the plug from behind.
      const from = c.from ?? c.at
      const way = turnBy({ x: 0, y: 1 }, c.rotation ?? 0)
      const leave = out ?? way
      const across = { x: -leave.y, y: leave.x }
      return (
        <g className="hw-lead">
          {points.map((p, i) => {
            const start = { x: from.x + across.x * (i - (points.length - 1) / 2) * 0.8, y: from.y + across.y * (i - (points.length - 1) / 2) * 0.8 }
            const end = { x: p.x - way.x * 1.2, y: p.y - way.y * 1.2 }
            const length = Math.hypot(end.x - start.x, end.y - start.y)
            const [first, last] = [Math.max(3, length * 0.45), Math.max(1, length * 0.4)]
            return <path key={i} className="hw-lead-wire" style={{ stroke: c.pins[i]?.color ?? '#888' }} d={`M ${start.x} ${start.y} C ${start.x + leave.x * first} ${start.y + leave.y * first}, ${end.x - way.x * last} ${end.y - way.y * last}, ${end.x} ${end.y}`} />
          })}
          {rect}
        </g>
      )
    }
  }
}
