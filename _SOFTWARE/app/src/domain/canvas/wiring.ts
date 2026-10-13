import type { CanvasBlock, ObjectPath, ProjectCanvas } from './types'

/*
 * Wires and variable chips on a canvas (features.md §8.1). Nothing new is
 * stored: a wire into an input is the input's path (`<block>:q<n>`), a wire
 * into EN is an entry of `enables`, a variable on an input is its path and a
 * variable on an output is `outputs[n]`. These helpers read and edit that.
 */

/** What travels on a wire: numbers and bools cast to each other; strings and whole objects don't. `any` = an empty chip, checked later by the compiler. */
export type WireKind = 'number' | 'bool' | 'string' | 'object' | 'any'

/** A block pin a wire starts at: an output, the ENO strip, or a FOR's body handle (canvas only). */
export interface WireSource {
  readonly block: string
  readonly pin: string
}

/** Where a wire or a chip ends: an input pin, the EN strip, or (variables only) an output pin. */
export type WireTarget =
  | { readonly block: string; readonly kind: 'in'; readonly index: number }
  | { readonly block: string; readonly kind: 'en'; /** One enable (its position), to take off; none = the strip. */ readonly index?: number }
  | { readonly block: string; readonly kind: 'out'; readonly index: number }

export interface Wire {
  readonly from: WireSource
  readonly to: WireTarget
  /** EN wires: position in `enables`. */
  readonly enableIndex?: number
}

const BLOCK_PIN = /^(.+):(q\d+|eno|body)$/

export const sourcePath = (source: WireSource): ObjectPath => ({ root: `${source.block}:${source.pin}` })

/** The block pin a path is, when it is a whole block pin on this canvas (not a variable). */
export const sourceOf = (path: ObjectPath | null | undefined, blocks: ReadonlyMap<string, CanvasBlock>): WireSource | undefined => {
  if (!path || path.steps?.length) return undefined
  const match = BLOCK_PIN.exec(path.root)
  return match && blocks.has(match[1]!) ? { block: match[1]!, pin: match[2]! } : undefined
}

/** Every block → block wire on a canvas (sources on the same canvas; other paths are variables). */
export const wiresOf = (canvas: ProjectCanvas): Wire[] => {
  const blocks = new Map(canvas.blocks.map((block) => [block.id, block]))
  return canvas.blocks.flatMap((block) => [
    ...(block.inputs ?? []).flatMap((path, index) => {
      const from = sourceOf(path, blocks)
      return from ? [{ from, to: { block: block.id, kind: 'in' as const, index } }] : []
    }),
    ...(block.enables ?? []).flatMap((path, enableIndex) => {
      const from = sourceOf(path, blocks)
      return from ? [{ from, to: { block: block.id, kind: 'en' as const }, enableIndex }] : []
    }),
  ])
}

const same = (a: ObjectPath, b: ObjectPath): boolean => JSON.stringify(a) === JSON.stringify(b)

/** A path (a wire or a variable) onto a target. An input is replaced, EN gets one more source (once), an output drives the object. */
export const connect = (block: CanvasBlock, target: WireTarget, path: ObjectPath): CanvasBlock => {
  if (target.kind === 'en') return (block.enables ?? []).some((entry) => same(entry, path)) ? block : { ...block, enables: [...(block.enables ?? []), path] }
  if (target.kind === 'out') {
    const outputs = Array.from({ length: Math.max(block.outputs?.length ?? 0, target.index + 1) }, (_, index) => block.outputs?.[index] ?? null)
    outputs[target.index] = path.root
    return { ...block, outputs }
  }
  const inputs = Array.from({ length: Math.max(block.inputs?.length ?? 0, target.index + 1) }, (_, index) => block.inputs?.[index] ?? null)
  inputs[target.index] = path
  return { ...block, inputs }
}

/** Removes what a target holds: an input or an output back to unwired, one EN source (by position) or all of them. */
export const disconnect = (block: CanvasBlock, target: WireTarget, enableIndex?: number): CanvasBlock => {
  if (target.kind === 'en') {
    const at = enableIndex ?? target.index
    const enables = at === undefined ? [] : (block.enables ?? []).filter((_, index) => index !== at)
    const { enables: _old, ...rest } = block
    return enables.length ? { ...rest, enables } : rest
  }
  if (target.kind === 'out') return { ...block, outputs: (block.outputs ?? []).map((entry, index) => (index === target.index ? null : entry)) }
  return { ...block, inputs: (block.inputs ?? []).map((entry, index) => (index === target.index ? null : entry)) }
}
