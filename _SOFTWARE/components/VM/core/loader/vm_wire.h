#pragma once
/**
 * @file vm_wire.h
 * @brief Wire records of the VM program class (0x04) and of VM telemetry.
 *
 * Every record the app sends to build, run and watch a program, as packed
 * little-endian structs. The decoder (dec_vm_loader.h), the loader and vm_sub
 * read and write records through these, and generate-vm-program.py publishes
 * them to data-structures/vm/vm-program.generated.json -- so the published
 * layout is the one the firmware parses. Grammar: vm-annotations.md
 * "Program wire format".
 *
 * A struct is the fixed part of a record; `//@tail` lines describe the
 * variable arrays that follow it, in order. A `@batch` packet body is
 * `u8 count` followed by that many records.
 */
#include <stdint.h>
#include <sys/cdefs.h>
#include "vm_obj.h"

// ===========================================================================
// 0x40 reset, 0x41 open
// ===========================================================================

//#vm-packet HEADER_packet_vm_reset @title Reset @when any @description Stop the program and unload it: objects, accessors, blocks, heap objects and subscriptions. Leaves the VM empty and stopped.
typedef struct __packed {
} vm_wire_reset_t;
_Static_assert(sizeof(vm_wire_reset_t) == 0, "0x40 has no body");

//#vm-packet HEADER_packet_vm_open @title Open program @when any @description Replace the loaded program with an empty one of this size. Stops a running program first (its retained values are saved). Nothing else can be loaded before it.
//@rule Open comes first: objects, values, accessors and blocks need an open program. @error ERR_VM_LOAD_BAD_STATE
//@rule total_size must cover every allocation (see arena) and fit CONFIG_VM_STORE_MAX_POOL; a failed open leaves the running program untouched. @error ERR_VM_LOAD_TOO_BIG
typedef struct __packed {
  uint16_t obj_cnt;     //@alias Objects @description Object IDs 0 to obj_cnt - 1.
  uint16_t acc_cnt;     //@alias Accessors @description Accessor IDs 0 to acc_cnt - 1.
  uint16_t blk_cnt;     //@alias Blocks @description Block IDs 0 to blk_cnt - 1.
  uint32_t total_size;  //@alias Arena Size @unit bytes @max CONFIG_VM_STORE_MAX_POOL @description Sum of every allocation of the program, see arena.
} vm_wire_open_t;
_Static_assert(sizeof(vm_wire_open_t) == 10, "0x41 body is 10 bytes");

// ===========================================================================
// 0x42 objects (also the telemetry describe record)
// ===========================================================================

//#vm-packet HEADER_packet_vm_add_objs @title Add objects @batch uint8_t @when stopped @description Create objects: shape, flags and name. Payloads start zeroed; values follow with Set values.
//@tail name char[head.d.name_size] @alias Name @encoding ascii @description Not NUL-terminated. Accessors find children by it; retained values are kept by it.
//@rule The ID is below obj_cnt. @error ERR_VM_REG_OOB
//@rule Each ID is added once. @error ERR_VM_REG_DUP
//@rule The type is a real type (not VM_OBJ_NONE). @error ERR_VM_OBJ_BAD_TYPE
//@rule payload_size is not 0. @error ERR_VM_OBJ_EMPTY
//@rule payload_size is a multiple of the type's memory width. @error ERR_VM_OBJ_BAD_SIZE
//@rule A PTR object can't be retentive. @error ERR_VM_OBJ_RETENTIVE_PTR
//@rule A retentive object needs a name. @error ERR_VM_RETAIN_UNNAMED
//@rule All retained values of the program fit CONFIG_VM_RETAIN_MAX_BYTES (2 + per object 4 + name + value). @error ERR_VM_RETAIN_TOO_BIG
typedef struct __packed {
  uint16_t id;                           //@alias Object ID @reference object
  uint8_t  head[VM_OBJ_HEAD_WIRE_SIZE];  //@alias Header @struct-ref vm_obj_head_t @description payload_size is the in-memory size (type memory width x elements).
} vm_wire_obj_t;
_Static_assert(sizeof(vm_wire_obj_t) == 2 + VM_OBJ_HEAD_WIRE_SIZE, "0x42 record head");

// ===========================================================================
// 0x43 values (also the telemetry value record)
// ===========================================================================

//#vm-packet HEADER_packet_vm_set_data @title Set values @batch uint8_t @when any @description While stopped: initial values, and the children of PTR objects. While running: a runtime write, applied at the start of the next pass.
//@tail data uint8_t[byte_len] @alias Data @description Elements in wire form, little-endian; a PTR element is its child's u16 object ID.
//@rule The object, and every child a PTR record links, exist before the record. @error ERR_VM_ACCESSOR_UNKNOWN_ID
//@rule Whole elements inside the object: byte_len is a multiple of the wire width and start_idx + elements <= the object's element count. @error ERR_VM_LOAD_DATA_RANGE
//@rule While running, PTR objects can't be written (links change only while stopped). @error ERR_VM_OVERRIDE_PTR_UNSUPPORTED
//@rule While running, only mutable objects can be written. @error ERR_VM_OBJ_NOT_MUTABLE
//@rule While running, block outputs (user-protected) can't be written. @error ERR_VM_OBJ_USR_PROTECTED
typedef struct __packed {
  uint16_t id;         //@alias Object ID @reference object
  uint16_t start_idx;  //@alias First Element @description Index of the first element written: elements, not bytes.
  uint16_t byte_len;   //@alias Length @unit bytes
} vm_wire_data_t;
_Static_assert(sizeof(vm_wire_data_t) == 6, "0x43 record head");

// ===========================================================================
// 0x44 accessors and their index steps
// ===========================================================================

//#vm-packet HEADER_packet_vm_add_acc @title Add accessors @batch uint8_t @when stopped @description A path to a value: a root object, then index steps through PTR children down to an element. Block pins read and write through accessors.
//@tail indices vm_wire_index[idx_count] @bytes idx_len @alias Path @description Index steps, back to back.
//@rule An accessor a REF step names is added before the accessor using it. @error ERR_VM_REG_OOB
//@rule idx_len covers exactly the idx_count steps. @error ERR_VM_LOAD_SHORT_RECORD
//@rule Every step kind is a vm_index_kind_e value. @error ERR_VM_ACC_BAD_KIND
//@rule A name step is at most VM_OBJ_NAME_MAX bytes. @error ERR_VM_OBJ_NAME_TOO_LONG
typedef struct __packed {
  uint16_t acc_id;       //@alias Accessor ID @reference accessor
  uint16_t root_obj_id;  //@alias Root Object @reference object @description May be added after the accessor (it then resolves on every use, slower).
  uint8_t  idx_count;    //@alias Steps
  uint8_t  idx_len;      //@alias Path Length @unit bytes
} vm_wire_acc_t;
_Static_assert(sizeof(vm_wire_acc_t) == 6, "0x44 record head");

//#vm-wire-union vm_wire_index @tag kind @enum-ref vm_index_kind_e @description One accessor path step: a u8 kind, then that kind's payload.
//@case $VM_IDX_LITERAL vm_wire_idx_literal_t
//@case $VM_IDX_REF vm_wire_idx_ref_t
//@case $VM_IDX_NAME vm_wire_idx_name_t

//#vm-wire-struct
typedef struct __packed {
  uint8_t  kind;   //@alias Kind @enum-ref vm_index_kind_e
  uint32_t value;  //@alias Position @description Child or element index.
} vm_wire_idx_literal_t;
_Static_assert(sizeof(vm_wire_idx_literal_t) == 5, "literal step");

//#vm-wire-struct
typedef struct __packed {
  uint8_t  kind;    //@alias Kind @enum-ref vm_index_kind_e
  uint16_t acc_id;  //@alias Accessor @reference accessor @description Its value, read on every use, is the position.
} vm_wire_idx_ref_t;
_Static_assert(sizeof(vm_wire_idx_ref_t) == 3, "reference step");

//#vm-wire-struct
//@tail name char[name_len] @alias Name @encoding ascii @description Not NUL-terminated.
typedef struct __packed {
  uint8_t kind;      //@alias Kind @enum-ref vm_index_kind_e
  uint8_t name_len;  //@alias Name Length @max VM_OBJ_NAME_MAX
} vm_wire_idx_name_t;
_Static_assert(sizeof(vm_wire_idx_name_t) == 2, "name step");

// ===========================================================================
// 0x45 block
// ===========================================================================

//#vm-packet HEADER_packet_vm_add_block @title Add block @when stopped @description One block per packet. Blocks run in block ID order, and a block's inputs must exist before it is added.
//@tail in_acc_ids uint16_t[in_cnt] @alias Inputs @reference accessor @none VM_BLOCK_NO_ID @description One accessor per input pin; VM_BLOCK_NO_ID leaves the pin unwired (the block uses its state constant).
//@tail out_obj_ids uint16_t[q_cnt] @alias Outputs @reference object @description One object per output pin. The device marks them user-protected: runtime writes to them are refused.
//@tail en_acc_ids uint16_t[en_cnt] @alias Enables @reference accessor @description Every one must exist; combined by en_mode.
//@tail custom_data uint8_t[custom_len] @alias State @description Initial private state; its layout per block type is in data-structures/vm/blocks/.
//@rule Every object and accessor the block names exists before it; the ENO object is a mutable B object. @error ERR_VM_BLK_BAD_REF
//@rule The block type exists in the palette. @error ERR_VM_BLK_UNKNOWN_TYPE
//@rule Pin counts within limits, en_mode and on_error known values, and the block passes its type's check (pins, required inputs, state); a rejected block is removed again. @error ERR_VM_BLK_BAD_SHAPE
typedef struct __packed {
  uint16_t blk_id;      //@alias Block ID @reference block @description Blocks run in ID order: 0 first.
  uint16_t block_idx;   //@alias Block Label @description The app's own number for the block; errors name the block by it.
  uint8_t  block_type;  //@alias Block Type @reference block-type
  uint8_t  in_cnt;      //@alias Inputs @max CONFIG_VM_BLOCK_MAX_IN
  uint8_t  q_cnt;       //@alias Outputs @max CONFIG_VM_BLOCK_MAX_OUT
  uint8_t  en_cnt;      //@alias Enables @max CONFIG_VM_BLOCK_MAX_EN @description 0 = always enabled.
  uint8_t  en_mode;     //@alias Enable Mode @enum-ref vm_blk_en_mode_e @one-of [$VM_BLK_EN_ANY, $VM_BLK_EN_ALL]
  uint8_t  on_error;    //@alias On Error @enum-ref vm_blk_on_error_e @one-of [$VM_BLK_ERR_STOP, $VM_BLK_ERR_CONTINUE]
  uint16_t custom_len;  //@alias State Length @unit bytes
  uint16_t eno_obj_id;  //@alias ENO Output @reference object @none VM_BLOCK_NO_ID @description Optional: a mutable B object that receives the block's ENO.
} vm_wire_block_t;
_Static_assert(sizeof(vm_wire_block_t) == 14, "0x45 fixed part");

// ===========================================================================
// 0x47 subscribe, 0x48 execution control
// ===========================================================================

//#vm-packet HEADER_packet_vm_subscribe @title Subscribe @batch uint8_t @batch-max CONFIG_VM_SUB_MAX_SUBSCRIBERS @when any @description Replace the telemetry list; 0 records clears it. Everything reachable is sent in full once, then whatever changes (see telemetry).
//@rule More IDs than CONFIG_VM_SUB_MAX_SUBSCRIBERS is refused and nothing changes. @error ERR_INVALID_VAL_UI32
typedef struct __packed {
  uint16_t id;  //@alias Object ID @reference object @description VM_OBJ_ID_DYN_BIT set for a heap object.
} vm_wire_sub_t;
_Static_assert(sizeof(vm_wire_sub_t) == 2, "0x47 record");

//#vm-packet HEADER_packet_vm_exec @title Execution control @when any @description Start, stop, step and reset the program.
typedef struct __packed {
  uint8_t command; //@required @alias Command @enum-ref vm_exec_command_e
} vm_wire_exec_t;
_Static_assert(sizeof(vm_wire_exec_t) == 1, "0x48 body");
