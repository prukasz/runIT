import { curveXAt } from '../domain/settings'
import type { ConfigCurve } from '../domain/settings'

/** A level drawn across the curve: a field of the settings with the curve's y unit (a battery's "Low at" / "Critical at"). */
export interface CurveLevel {
  readonly label: string
  readonly value: number
}

const WIDTH = 520
const HEIGHT = 260
const PAD = { left: 48, right: 20, top: 14, bottom: 34 }
const TICKS = 5
const LEVEL_COLORS = ['var(--color-amber)', 'var(--color-red)', 'var(--color-blue-bright)']

const trim = (value: number): string => String(Math.round(value * 100) / 100)

/** The curve a firmware table publishes, drawn: its points, the grid, and the levels the user set across it with the x each one means. */
export function CurveChart({ curve, levels }: { curve: ConfigCurve; levels: readonly CurveLevel[] }) {
  const xs = curve.points.map(([x]) => x)
  const ys = curve.points.map(([, y]) => y)
  const [xMin, xMax, yMin, yMax] = [Math.min(...xs), Math.max(...xs), Math.min(...ys), Math.max(...ys)]
  const sx = (x: number) => PAD.left + ((x - xMin) / (xMax - xMin)) * (WIDTH - PAD.left - PAD.right)
  const sy = (y: number) => HEIGHT - PAD.bottom - ((y - yMin) / (yMax - yMin)) * (HEIGHT - PAD.top - PAD.bottom)
  const line = [...curve.points].sort((a, b) => a[0] - b[0]).map(([x, y]) => `${sx(x).toFixed(1)},${sy(y).toFixed(1)}`).join(' ')
  const ticks = Array.from({ length: TICKS }, (_, index) => index / (TICKS - 1))

  return (
    <figure className="curve-chart">
      <svg viewBox={`0 0 ${WIDTH} ${HEIGHT}`} role="img" aria-label={curve.alias}>
        {ticks.map((fraction) => {
          const y = yMin + fraction * (yMax - yMin)
          const x = xMin + fraction * (xMax - xMin)
          return (
            <g key={fraction} className="curve-grid">
              <line x1={PAD.left} x2={WIDTH - PAD.right} y1={sy(y)} y2={sy(y)} />
              <text x={PAD.left - 6} y={sy(y) + 4} textAnchor="end">{trim(y)}</text>
              <line className="is-vertical" x1={sx(x)} x2={sx(x)} y1={PAD.top} y2={HEIGHT - PAD.bottom} />
              <text x={sx(x)} y={HEIGHT - PAD.bottom + 16} textAnchor="middle">{trim(x)}</text>
            </g>
          )
        })}
        <text className="curve-axis" x={(PAD.left + WIDTH - PAD.right) / 2} y={HEIGHT - 4} textAnchor="middle">{curve.x.alias} ({curve.x.unit})</text>
        <text className="curve-axis" x={12} y={(PAD.top + HEIGHT - PAD.bottom) / 2} textAnchor="middle" transform={`rotate(-90 12 ${(PAD.top + HEIGHT - PAD.bottom) / 2})`}>{curve.y.alias} ({curve.y.unit})</text>
        <polyline className="curve-line" points={line} />
        {levels.map((level, index) => {
          if (level.value < yMin || level.value > yMax) return null
          const at = curveXAt(curve, level.value)
          const color = LEVEL_COLORS[index % LEVEL_COLORS.length]
          return (
            <g key={level.label} className="curve-level" style={{ color }}>
              <line x1={PAD.left} x2={WIDTH - PAD.right} y1={sy(level.value)} y2={sy(level.value)} />
              {at !== undefined && <circle cx={sx(at)} cy={sy(level.value)} r={4} />}
              <text x={WIDTH - PAD.right - 4} y={sy(level.value) - 5} textAnchor="end">{level.label} {level.value} {curve.y.unit}{at !== undefined ? ` ≈ ${Math.round(at)} ${curve.x.unit}` : ''}</text>
            </g>
          )
        })}
      </svg>
      <figcaption>{curve.alias}: {curve.description}.</figcaption>
    </figure>
  )
}
