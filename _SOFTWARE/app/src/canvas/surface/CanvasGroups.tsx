import { arrangeProgram } from '../../domain/canvas'
import type { VmCatalog } from '../../domain/descriptors'
import type { CanvasBlock, ProjectCanvas } from '../../domain/project'
import type { BlockShape } from '../blocks/blockView'

/*
 * Branches and loops as rounded, lightly tinted areas behind the blocks
 * (features.md §8.1): one per gate (IF yes / no, a SWITCH case, a tick, an
 * EDGE pulse, an ENO) around the blocks that run in it, and one per FOR around
 * it and its body. A block is in a branch directly, through the blocks feeding
 * it, or through the block gating it (IF₂ inside IF₁'s branch takes what it
 * gates along). An area holding another grows to wrap it; inner areas are
 * drawn over outer ones. They come from the same arrangement the compiler
 * uses, so what is drawn is what runs.
 */

const PAD = 12
/** Extra margin per area nested inside. */
const NEST = 12
const RADIUS = 12
const TINTS = Array.from({ length: 8 }, (_, index) => `var(--group-tint-${index + 1})`)
const BLOCK_PIN = /^(.+):(q\d+|eno)$/

export interface CanvasGroup {
  readonly key: string
  readonly label: string
  readonly members: readonly CanvasBlock[]
  readonly loop: boolean
  /** How many areas nest inside it (0 = innermost). */
  readonly depth: number
}

type P = readonly [number, number]

/** Convex hull (monotone chain), counter-clockwise. */
const hull = (points: readonly P[]): P[] => {
  const sorted = [...points].sort((a, b) => a[0] - b[0] || a[1] - b[1])
  if (sorted.length < 3) return sorted
  const cross = (o: P, a: P, b: P) => (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
  const half = (list: readonly P[]) => {
    const out: P[] = []
    for (const point of list) {
      while (out.length >= 2 && cross(out[out.length - 2]!, out[out.length - 1]!, point) <= 0) out.pop()
      out.push(point)
    }
    return out.slice(0, -1)
  }
  return [...half(sorted), ...half([...sorted].reverse())]
}

/** The corners of a block grown by `pad`, each rounded (sampled on a quarter circle). */
const roundedCorners = (block: CanvasBlock, shape: BlockShape, pad: number): P[] => {
  const inset = pad - RADIUS
  const corners: [number, number, number][] = [
    [block.x - inset, block.y - inset, Math.PI],
    [block.x + shape.width + inset, block.y - inset, 1.5 * Math.PI],
    [block.x + shape.width + inset, block.y + shape.height + inset, 0],
    [block.x - inset, block.y + shape.height + inset, 0.5 * Math.PI],
  ]
  return corners.flatMap(([cx, cy, start]) => Array.from({ length: 7 }, (_, step) => {
    const angle = start + (step / 6) * (Math.PI / 2)
    return [cx + RADIUS * Math.cos(angle), cy + RADIUS * Math.sin(angle)] as P
  }))
}

/** The gate and loop areas of one canvas, outermost first. */
export const canvasGroups = (canvas: ProjectCanvas, catalog: VmCatalog): CanvasGroup[] => {
  const blocks = new Map(canvas.blocks.map((block) => [block.id, block]))
  const { blocks: ordered, gates } = arrangeProgram([{ ...canvas, disabled: false }])
  const labels = new Map<string, { label: string; loop: boolean }>()
  // Areas per block, sources first so a gating block's areas are known.
  const areasOf = new Map<string, Set<string>>()
  for (const { id } of ordered) {
    const block = blocks.get(id)
    if (!block) continue
    const areas = new Set<string>()
    const entry = gates.get(id)
    for (const path of entry?.enables ?? []) {
      const match = path.steps?.length ? null : BLOCK_PIN.exec(path.root)
      const source = match && blocks.get(match[1]!)
      if (!source || !match) continue
      const pin = match[2]!
      // ENO is ordinary flow from most blocks. It only defines an area when
      // used as a FOR loop link; arrangeProgram reports those separately.
      if (pin === 'eno') continue
      const pins = catalog.block(source.type)?.outputs.pins ?? []
      const pinDescriptor = pins[Math.min(Number(pin.slice(1)), pins.length - 1)]
      // Value outputs are data flow, not branch gates. Only gate-valued
      // outputs (IF, SWITCH, etc.) create a contour.
      if (pinDescriptor?.value !== 'gate') continue
      const title = source.outputAliases?.[Number(pin.slice(1))] || pinDescriptor.title || pin
      labels.set(path.root, { label: `${source.name || source.id} · ${title}${source.type === 'SWITCH' && pin !== 'eno' ? ` ${pin.slice(1)}` : ''}`, loop: false })
      areas.add(path.root)
      for (const area of areasOf.get(source.id) ?? []) areas.add(area)
    }
    for (const loop of entry?.loops ?? []) {
      labels.set(`${loop}:loop`, { label: `${blocks.get(loop)?.name || loop} · loop`, loop: true })
      areas.add(`${loop}:loop`)
    }
    // A block gated by a member of an area is in it too (its own ENO / gate link).
    for (const path of block.enables ?? []) {
      const source = BLOCK_PIN.exec(path.root)?.[1]
      if (source && blocks.has(source)) for (const area of areasOf.get(source) ?? []) areas.add(area)
    }
    areasOf.set(id, areas)
  }
  const members = new Map<string, CanvasBlock[]>()
  for (const [id, areas] of areasOf) for (const area of areas) members.set(area, [...(members.get(area) ?? []), blocks.get(id)!])
  // A loop's area holds the FOR itself.
  for (const [key, list] of members) if (key.endsWith(':loop')) {
    const owner = blocks.get(key.slice(0, -5))
    if (owner && !list.includes(owner)) list.unshift(owner)
  }

  const keys = [...members.keys()]
  const inside = (inner: string, outer: string) => {
    const a = members.get(inner)!
    const b = new Set(members.get(outer)!)
    if (!a.every((block) => b.has(block))) return false
    // The same blocks: the later area counts as the inner one.
    return a.length < b.size || keys.indexOf(inner) > keys.indexOf(outer)
  }
  const depth = new Map<string, number>()
  const depthOf = (key: string): number => {
    if (!depth.has(key)) depth.set(key, Math.max(-1, ...keys.filter((other) => other !== key && inside(other, key)).map(depthOf)) + 1)
    return depth.get(key)!
  }
  return keys
    .map((key) => ({ key, ...labels.get(key)!, members: members.get(key)!, depth: depthOf(key) }))
    .sort((a, b) => b.depth - a.depth)
}

export function CanvasGroups({ canvas, catalog, shapeOf }: { canvas: ProjectCanvas; catalog: VmCatalog; shapeOf: (block: CanvasBlock) => BlockShape }) {
  const groups = canvasGroups(canvas, catalog)
  if (!groups.length) return null
  const tints = new Map([...groups].sort((a, b) => a.key.localeCompare(b.key)).map((group, index) => [group.key, TINTS[index % TINTS.length]!]))
  return (
    <svg className="canvas-groups" aria-hidden="true">
      {groups.map((group) => {
        const pad = PAD + NEST * group.depth
        const outline = hull(group.members.flatMap((block) => roundedCorners(block, shapeOf(block), pad)))
        const d = `M ${outline.map(([x, y]) => `${x.toFixed(1)} ${y.toFixed(1)}`).join(' L ')} Z`
        const top = outline.reduce((best, point) => (point[1] < best[1] - 0.5 || (Math.abs(point[1] - best[1]) <= 0.5 && point[0] < best[0]) ? point : best))
        return (
          <g key={group.key} className={`canvas-group ${group.loop ? 'is-loop' : ''}`} style={{ '--tint': tints.get(group.key) } as React.CSSProperties}>
            <path className="canvas-group-area" d={d} />
            <text className="canvas-group-label" x={top[0] + 4} y={top[1] - 5}>{group.label}</text>
          </g>
        )
      })}
    </svg>
  )
}
