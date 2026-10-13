import type { ObjectNode } from '../project'

/** Kind of a value for wiring: what a variable dragged from the tree can be dropped on. */
export type WireKind = 'number' | 'bool' | 'string' | 'object' | 'any'

/** Drag data of a variable dragged from the tree: its project ID. */
export const OBJECT_DRAG_TYPE = 'application/x-runit-object'
/** A second drag type carrying the variable's kind: drag-over events can read types, not data. */
export const objectKindDragType = (kind: WireKind): string => `application/x-runit-kind-${kind}`

/** Kind of a variable from the tree. */
export const variableKind = (node: ObjectNode | undefined): WireKind => {
  if (!node) return 'any'
  if (node.kind !== 'value') return 'object'
  return node.type === 'B' ? 'bool' : node.type === 'STR' ? 'string' : 'number'
}
