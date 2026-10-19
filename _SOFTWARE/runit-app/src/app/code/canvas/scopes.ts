import type { BlockInstance, Catalog, Source } from '@engine'
import { blockName, blockPinName, pinsOf } from '../blocks'
import type { BlockPin } from '../blocks'
import { GRID } from './layout'
import type { Rect } from './layout'

/*
 * The scopes of the canvas, pure: what runs only under a branch (a conditional output: if's Yes / No, a switch's case) or in
 * a loop's body (the enable-out of a block with a span: for). A scope holds the blocks whose Run when reads that pin, and
 * every block whose Run when reads one of those, on and on; it is drawn as a tinted area behind them, named after the block
 * and the pin. A scope inside another's blocks sits inside its area, so the nesting shows.
 */

export interface Scope {
  readonly key: string
  readonly block: string
  readonly pin: BlockPin
  /** `If · Yes`, `For · Loop body`. */
  readonly label: string
  readonly members: readonly string[]
  readonly rect: Rect
  /** How many scopes hold this one. */
  readonly depth: number
  /** Position in a small palette, stable for the block and pin. */
  readonly hue: number
}

/** Room around the blocks of a scope, and above them for its name. */
const PAD = GRID
const LABEL = 22
const HUES = 6

const fromBlock = (source: Source, block: string, pin: BlockPin): boolean =>
  source.kind === 'variable' ? false : source.block === block && (pin === 'eno' ? source.kind === 'eno' : source.kind === 'output' && source.pin === pin)

const union = (rects: readonly Rect[]): Rect => {
  const x = Math.min(...rects.map((r) => r.x))
  const y = Math.min(...rects.map((r) => r.y))
  return { x, y, width: Math.max(...rects.map((r) => r.x + r.width)) - x, height: Math.max(...rects.map((r) => r.y + r.height)) - y }
}

export const scopesOf =(catalog: Catalog, blocks: readonly BlockInstance[], rectOf: (block: BlockInstance) => Rect): Scope[] => {
  const byId = new Map(blocks.map((b) => [b.id, b]))
  /** The pins that open a scope: each conditional output, and a loop's enable-out. */
  const roots = blocks.flatMap((block): { block: BlockInstance; pin: BlockPin }[] => [
    ...(pinsOf(catalog, block)?.outputs ?? []).filter((p) => p.pin.flow === 'conditional').map((p) => ({ block, pin: p.index as BlockPin })),
    ...(catalog.get(block.type)?.span ? [{ block, pin: 'eno' as const }] : []),
  ])
  const found = roots.flatMap(({ block, pin }, n) => {
    const members = new Set<string>()
    const queue = blocks.filter((b) => b.enables.some((s) => fromBlock(s, block.id, pin))).map((b) => b.id)
    while (queue.length) {
      const id = queue.pop()!
      if (members.has(id) || id === block.id) continue
      members.add(id)
      for (const b of blocks) if (b.enables.some((s) => s.kind !== 'variable' && s.block === id)) queue.push(b.id)
    }
    return members.size ? [{ key: `${block.id}:${pin}`, block: block.id, pin, members, hue: n % HUES }] : []
  })

  // A scope's area holds its blocks and the areas of the scopes opened inside it (their block is one of its members).
  const rects = new Map<string, Rect>()
  const depths = new Map<string, number>()
  const area = (scope: (typeof found)[number], seen: ReadonlySet<string>): Rect => {
    const known = rects.get(scope.key)
    if (known) return known
    const inner = found.filter((s) => s !== scope && scope.members.has(s.block) && !seen.has(s.key))
    const next = new Set([...seen, scope.key])
    const box = union([...[...scope.members].map((id) => rectOf(byId.get(id)!)), ...inner.map((s) => area(s, next))])
    const rect = { x: box.x - PAD, y: box.y - PAD - LABEL, width: box.width + 2 * PAD, height: box.height + 2 * PAD + LABEL }
    rects.set(scope.key, rect)
    return rect
  }
  for (const scope of found) {
    area(scope, new Set())
    depths.set(scope.key, found.filter((s) => s !== scope && s.members.has(scope.block)).length)
  }
  return found.map((scope) => ({
    key: scope.key,
    block: scope.block,
    pin: scope.pin,
    label: `${blockName(catalog, blocks, scope.block)} · ${blockPinName(catalog, byId.get(scope.block), scope.pin)}`,
    members: [...scope.members],
    rect: rects.get(scope.key)!,
    depth: depths.get(scope.key)!,
    hue: scope.hue,
  }))
}
