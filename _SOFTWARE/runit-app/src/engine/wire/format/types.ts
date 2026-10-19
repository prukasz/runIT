/*
 * The vocabulary of the VM wire format. Sources: runit-esp/components/vm/vm_core/obj/vm_obj.h (types, widths, name limit),
 * loader/vm_wire.h (records), store/vm_store.h (ids). Numbers that firmware publishes (op ids, enum values) are not here:
 * they arrive in a WireDefinitions (definitions.ts), read from generated/.
 */

/** Element type of an object (`vm_obj_t_e`, minus VM_OBJ_NONE). */
export const OBJ_TYPES = ['ptr', 'u8', 'u32', 'i32', 'f32', 'bool', 'str'] as const
export type ObjType = (typeof OBJ_TYPES)[number]

export type Scalar = number | boolean
/** A value as the user states it: one element, a list, or raw bytes (already in wire form). */
export type Value = Scalar | readonly Scalar[] | Uint8Array

/**
 * Bytes of one element on the wire and in the board's memory. A PTR element is its child's u16 object id on the wire and a
 * 4-byte pointer in memory (`VM_OBJ_PTR_WIRE_SIZE`, `vm_obj_type_sizes` in vm_obj.h). `payload_size` counts memory bytes.
 */
export const OBJ_WIDTH: Readonly<Record<ObjType, { readonly wire: number; readonly mem: number }>> = {
  ptr: { wire: 2, mem: 4 },
  u8: { wire: 1, mem: 1 },
  u32: { wire: 4, mem: 4 },
  i32: { wire: 4, mem: 4 },
  f32: { wire: 4, mem: 4 },
  bool: { wire: 1, mem: 1 },
  str: { wire: 1, mem: 1 },
}

/** Longest object tag (`VM_OBJ_NAME_MAX`: the name length is 4 bits in the object head). */
export const NAME_MAX = 15

/** "No id": an unwired pin, no ENO. The board refuses it as the id of a new object, accessor or block (`VM_ID_NONE`, `VM_BLOCK_NO_ID`). */
export const ID_NONE = 0xffff

/** Bytes of the object head on the wire (`VM_OBJ_HEAD_WIRE_SIZE`) and in memory (`sizeof(vm_obj_head_t)`). */
export const OBJ_HEAD_SIZE = 4

/** Bytes of the fixed part of a block in memory (`sizeof(vm_block_data_t)`) and of one pin pointer. */
export const BLOCK_HEADER_SIZE = 16
export const POINTER_SIZE = 4

/** Bytes of an accessor header and of one index step in memory (`sizeof(vm_accessor_t)`, `sizeof(vm_index_t)`). They feed the arena size. */
export const ACCESSOR_HEADER_SIZE = 20
export const INDEX_STEP_SIZE = 8

/** One step of an accessor path, in wire terms: a fixed element, an element named by another accessor's value, or a child by name. */
export type IndexStep =
  | { readonly kind: 'literal'; readonly value: number }
  | { readonly kind: 'ref'; readonly accessor: number }
  | { readonly kind: 'name'; readonly name: string }

/** Execution control commands the app sends (`vm_exec_command_e`). */
export const EXEC_COMMANDS = ['scan', 'once', 'block', 'next', 'restart', 'normal', 'pause', 'resume', 'reset', 'ackFault', 'retainClear'] as const
export type ExecCommand = (typeof EXEC_COMMANDS)[number]
