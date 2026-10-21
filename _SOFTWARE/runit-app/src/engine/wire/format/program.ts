import type { IndexStep, ObjType } from './types'

/*
 * A program at the level of the wire: numeric ids, bytes, nothing a user would read. The compiler (compiler/) produces it
 * from a project; the frame builder (frames.ts) turns it into load frames. Ids are dense registry slots: object, accessor
 * and block ids each run 0..count-1 (`vm_wire_open_t`), and a record's id says which slot it fills.
 */

export interface ObjectFlags {
  /** Writable at run time (`VM_OBJ_F_MUTABLE`). */
  readonly mutable?: boolean
  /** Freshness cleared at the end of every pass: needed for a block's `upd` triggering to see a write (`VM_OBJ_F_UPD_RESETABLE`). */
  readonly updResetable?: boolean
  /** Kept across restarts; the object must be named (`VM_OBJ_F_RETENTIVE`, `vm_retain_reserve`). */
  readonly retentive?: boolean
  /** A user's run-time writes are refused (`VM_OBJ_F_USR_PROTECTED`). */
  readonly usrProtected?: boolean
}

export interface ObjectDef {
  readonly id: number
  readonly type: ObjType
  /** Elements: 1 for a scalar, n for an array, the child count for a folder (PTR). */
  readonly count: number
  readonly flags: ObjectFlags
  /** The tag, at most NAME_MAX bytes of UTF-8. Empty = untagged. */
  readonly name: string
}

/** The initial value of an object, in wire bytes, from element `start`. A PTR object takes child ids (u16 each). */
export interface DataDef {
  readonly id: number
  readonly start: number
  readonly data: Uint8Array
}

export interface AccessorDef {
  readonly id: number
  /** The object the path starts at. */
  readonly root: number
  readonly steps: readonly IndexStep[]
}

export interface BlockDef {
  readonly id: number
  /** The block's place in the program as the board reports it (`block_idx`); equal to `id` for a program the app builds. */
  readonly index: number
  /** `wire_id` of the block type (`block_type` of the ADD_BLOCK record). */
  readonly type: number
  /** An accessor id per input pin; ID_NONE = unwired. */
  readonly inputs: readonly number[]
  /** An object id per output pin. */
  readonly outputs: readonly number[]
  /** An accessor id per enable source; none listed = always enabled. */
  readonly enables: readonly number[]
  readonly enableMode: 'any' | 'all'
  readonly onError: 'stop' | 'continue'
  /** The block's private state (`custom_data`), its length is `custom_len`. */
  readonly state: Uint8Array
  /** The object that receives the block's ENO (a mutable bool), or ID_NONE. */
  readonly eno: number
}

export interface ProgramIR {
  readonly objects: readonly ObjectDef[]
  readonly data: readonly DataDef[]
  readonly accessors: readonly AccessorDef[]
  /** In execution order. */
  readonly blocks: readonly BlockDef[]
}
