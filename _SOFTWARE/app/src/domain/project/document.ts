/*
 * The project document. The file is the source of truth; stable string IDs, never the VM's wire IDs.
 * So far it holds the object tree; the program, devices and settings join it as their editors come.
 */

export const PROJECT_FORMAT = 'runit-project'
export const PROJECT_FORMAT_VERSION = 1

export interface ProjectDocument {
  readonly format: typeof PROJECT_FORMAT
  readonly format_version: number
  readonly name: string
  /** Top-level objects and folders, in display order. */
  readonly objects: readonly ObjectNode[]
}

/** Initial values: numbers for numeric types, booleans for B, a string for STR, missing = zeros. */
export type ObjectValue = readonly number[] | readonly boolean[] | string

/** A folder is a VM_OBJ_PTR object whose elements are its children, in order. */
export interface FolderNode {
  readonly kind: 'folder'
  readonly id: string
  readonly name: string
  readonly description?: string
  readonly children: readonly ObjectNode[]
  /** Track live updates / telemetry stream subscription. */
  readonly subscribed?: boolean
}

export interface ValueNode {
  readonly kind: 'value'
  readonly id: string
  readonly name: string
  readonly description?: string
  /** VM object type without `VM_OBJ_`: `U8`, `U32`, `I32`, `F`, `B`, `STR`. */
  readonly type: string
  /** Elements; for STR the capacity in bytes. */
  readonly length: number
  readonly value?: ObjectValue
  /** Writable at run time (by blocks and by the app). false = a constant. */
  readonly mutable: boolean
  /** Kept across restarts by the device, by name. */
  readonly retentive: boolean
  /** Automatic type inference in the editor; absent = manual. */
  readonly typeMode?: 'auto'
  /** Track live updates / telemetry stream subscription. */
  readonly subscribed?: boolean
}

/** A non-owning tree entry pointing at a value or folder defined elsewhere. */
export interface ReferenceNode {
  readonly kind: 'reference'
  readonly id: string
  readonly name: string
  readonly description?: string
  readonly targetId: string
}

export type ObjectNode = FolderNode | ValueNode | ReferenceNode

export const createProject = (name: string): ProjectDocument => ({
  format: PROJECT_FORMAT,
  format_version: PROJECT_FORMAT_VERSION,
  name,
  objects: [],
})
