/*
 * What a canvas holds. The stored form is plain data: a wire into an input is the input's path (`<block>:q<n>`), a wire into EN is
 * an entry of `enables`, a variable on an input is its path, a variable on an output is `outputs[n]`.
 */

/**
 * What a block pin reads or writes: an object of the tree, then steps below it. A path that ends on a folder's child is its slot
 * in the folder, not the child itself.
 */
export interface ObjectPath {
  /** Project ID of a value, folder or reference, or `<block id>:q<n>` / `<block id>:eno` for a block-owned one. */
  readonly root: string
  readonly steps?: readonly PathStep[]
}

export type PathStep =
  | { readonly kind: 'index'; readonly index: number }
  | { readonly kind: 'name'; readonly name: string }
  | { readonly kind: 'dynamic'; readonly index: ObjectPath }

/** One block of a program. */
export interface ProgramBlock {
  readonly id: string
  /** Block type key: `EXPR`, `PERIODIC` ... (the descriptor's name without `VM_BLK_`). */
  readonly type: string
  /** Input pins in order: a path, or null = unwired. */
  readonly inputs?: readonly (ObjectPath | null)[]
  /** Output pins in order: a user object (project ID) the block drives, or null for a block-owned object. */
  readonly outputs?: readonly (string | null)[]
  /** Enable sources dropped on the EN strip (a gate, an ENO, a bool). None = always enabled. */
  readonly enables?: readonly ObjectPath[]
  readonly enableMode?: 'any' | 'all'
  /** Its state fields by name: a number, or an enum member name. */
  readonly settings?: Readonly<Record<string, number | string>>
}

/** A block on a canvas, and where its top-left corner sits (canvas units, px at 100 % zoom). */
export interface CanvasBlock extends ProgramBlock {
  readonly x: number
  readonly y: number
  /** The user's name for the block, shown on the canvas (its ID stays the reference). */
  readonly name?: string
  /** Optional names for block-owned output pins, by output index. */
  readonly outputAliases?: readonly (string | null)[]
}

/** One canvas (a flow tab). A project is one program; every enabled canvas's blocks run in list order. */
export interface ProjectCanvas {
  readonly id: string
  readonly name: string
  readonly disabled?: boolean
  readonly blocks: readonly CanvasBlock[]
}
