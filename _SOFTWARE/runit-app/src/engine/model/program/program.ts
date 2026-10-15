import type { FieldScalar } from '../../catalog/types'
import type { ObjectPath } from '../paths/paths'
import type { Variable } from '../variables/variables'

/*
 * The program the user builds: variables and blocks. Plain, serializable data; the compiler (compiler/) turns it into the
 * frames the board loads. A block is an instance of a catalog block type (catalog/), wired to variables and to other blocks.
 *
 * Pins are addressed by position. A block's concrete pins are the type's pins (and the chosen variant's) with every group
 * expanded to the count in `groups`: `shapeOf(type, variant)` lists them, and the n-th of its inputs is `inputs[n]` here.
 */

/** Where a pin reads from. */
export type Source =
  /** A variable (or an element of one), by path. */
  | { readonly kind: 'variable'; readonly path: ObjectPath }
  /** An output pin of another block, by position among that block's outputs. */
  | { readonly kind: 'output'; readonly block: string; readonly pin: number }
  /** The enable-out of another block: "it ran" (or what the block says it means: a pulse, a held level). */
  | { readonly kind: 'eno'; readonly block: string }

/** Where an output pin writes besides its own object: a user variable. `null`: nowhere else. */
export type Target = { readonly path: ObjectPath } | null

export type FieldValue = FieldScalar

export interface BlockInstance {
  /** Stable id inside the project. */
  readonly id: string
  /** The block type (`BlockType.id`). */
  readonly type: string
  /** The variant, for a block that has them; the first when unset. */
  readonly variant?: string
  /** What the user calls this block; empty = the type's name. */
  readonly name?: string
  /** For a device block: the id of the device it works on (`null` while none is chosen). */
  readonly device?: number | null
  /** Pins the user added to a group: how many inputs and outputs the block's group (if any) has. */
  readonly groups?: { readonly inputs?: number; readonly outputs?: number }
  /** By input position; `null` or a missing entry = unwired (a field's own value then stands in, if it has one). */
  readonly inputs: readonly (Source | null)[]
  /** By output position: the user variable the block also writes, or `null`. */
  readonly outputs: readonly Target[]
  /** The enable sources; none = always enabled. */
  readonly enables: readonly Source[]
  readonly enableMode?: 'any' | 'all'
  readonly onError?: 'stop' | 'continue'
  /** Field values by field id (enum option values, numbers, booleans). A field without one takes its default. */
  readonly values?: Readonly<Record<string, FieldValue>>
  /** What the block's editor owns (an expression's text, a switch's case names); the codec of the block type reads it. */
  readonly editor?: unknown
}

export interface Project {
  readonly variables: readonly Variable[]
  /** The user's order. Execution order follows the wiring and falls back to this one (graph/arrange.ts). */
  readonly blocks: readonly BlockInstance[]
}

/** A block of a given type with nothing wired. */
export const newBlock = (id: string, type: string, patch: Partial<Omit<BlockInstance, 'id' | 'type'>> = {}): BlockInstance => ({
  id,
  type,
  inputs: [],
  outputs: [],
  enables: [],
  ...patch,
})
