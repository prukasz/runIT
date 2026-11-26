import './Sparkline.css'

/** A value from `x` (0: the window's start, 1: now) until the next point's; undefined `y`: nothing to draw there. */
export interface SparkPoint {
  readonly x: number
  readonly y: number | undefined
}

const WIDTH = 100
const HEIGHT = 32
/** Room above and below, so a line at the range's end is not cut. */
const INSET = 2

/** A value over time as steps: each holds until the next, the last one until now. */
export function Sparkline({ points, min, max, label, size = 'row' }: {
  points: readonly SparkPoint[]
  min: number
  max: number
  /** What it shows, for a reader that does not see it. */
  label: string
  size?: 'row' | 'panel'
}) {
  const span = max - min || 1
  const at = (x: number) => Math.min(1, Math.max(0, x)) * WIDTH
  const up = (y: number) => HEIGHT - INSET - ((y - min) / span) * (HEIGHT - 2 * INSET)
  let path = ''
  let drawing = false
  points.forEach((point, i) => {
    if (point.y === undefined) {
      drawing = false
      return
    }
    const next = points[i + 1]?.x ?? 1
    path += `${drawing ? ' L' : ' M'}${at(point.x)} ${up(point.y)} L${at(next)} ${up(point.y)}`
    drawing = true
  })
  return (
    <svg className={`sparkline is-${size}`} viewBox={`0 0 ${WIDTH} ${HEIGHT}`} preserveAspectRatio="none" role="img" aria-label={label}>
      <path d={path.trim()} />
    </svg>
  )
}
