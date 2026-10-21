#pragma once
/**
 * @file vm_wire.h
 * @brief Wire records of the VM router (ROUTER_KEY_VM, vm_router.h) and of VM telemetry.
 *
 * Every record the app sends to build, run and watch a program, as packed
 * little-endian structs. The router (vm_router.c), the loader and vm_sub
 * read and write records through these.
 *
 * A struct is the fixed part of a record; any variable arrays follow it, in order. A batch
 * packet body is `u8 count` followed by that many records.
 */

#include "vm_obj.h"

// ===========================================================================
// RESET, OPEN
// ===========================================================================

typedef struct __packed {
} vm_wire_reset_t;
_Static_assert(sizeof(vm_wire_reset_t) == 0, "RESET has no body");

typedef struct __packed {
  uint16_t obj_cnt;     /**< Object slots */
  uint16_t acc_cnt;     /**< Accessor slots */
  uint16_t blk_cnt;     /**< Block slots */
  uint32_t total_size;  /**< Arena size in bytes reserved for the program */
} vm_wire_open_t;
_Static_assert(sizeof(vm_wire_open_t) == 10, "OPEN body is 10 bytes");

// ===========================================================================
// ADD_OBJECTS (also the telemetry describe record)
// ===========================================================================

/* The object head on the wire: four bytes, explicit layout, not the compiler's bitfield layout.
 *   [0..1] u16 payload_size (LE)
 *   [2]    bits 0..3 element type (vm_obj_t_e), bits 4..7 name length (0..15)
 *   [3]    flags, the object's VM_OBJ_F_* (vm_obj.h)
 * Built and read only with vm_wire_head_pack / _unpack. */
#define VM_WIRE_HEAD_TYPE_MASK  0x0Fu
#define VM_WIRE_HEAD_NAME_SHIFT 4u

typedef struct __packed {
  uint16_t payload_size;
  uint8_t  type_name;  // type | name length << 4
  uint8_t  flags;
} vm_wire_head_t;
_Static_assert(sizeof(vm_wire_head_t) == VM_OBJ_HEAD_WIRE_SIZE, "the object head is 4 bytes on the wire");

typedef struct __packed {
  uint16_t       id;
  vm_wire_head_t head;
} vm_wire_obj_t;
_Static_assert(sizeof(vm_wire_obj_t) == 2 + VM_OBJ_HEAD_WIRE_SIZE, "ADD_OBJECTS record head");

/* Wire head -> object head. Only the shape and the flags the app may set come over: mutable, retentive, usr_protected
 * (tagged and dynamic are the board's). */
static inline vm_obj_head_t vm_wire_head_unpack(const vm_wire_head_t* w) {
  vm_obj_head_t h = {0};
  h.payload_size = w->payload_size;
  h.d.obj_t = w->type_name & VM_WIRE_HEAD_TYPE_MASK;
  h.d.name_size = (uint8_t)(w->type_name >> VM_WIRE_HEAD_NAME_SHIFT);
  h.f.mutable = (w->flags & VM_OBJ_F_MUTABLE) != 0;
  h.f.retentive = (w->flags & VM_OBJ_F_RETENTIVE) != 0;
  h.f.usr_protected = (w->flags & VM_OBJ_F_USR_PROTECTED) != 0;
  return h;
}

/* Object head -> wire head (telemetry), with the name length `name_len` the record carries. */
static inline vm_wire_head_t vm_wire_head_pack(const vm_obj_head_t* h, uint8_t name_len) {
  vm_wire_head_t w = {.payload_size = h->payload_size,
                      .type_name = (uint8_t)((h->d.obj_t & VM_WIRE_HEAD_TYPE_MASK) | (name_len << VM_WIRE_HEAD_NAME_SHIFT))};
  w.flags = (uint8_t)((h->f.mutable ? VM_OBJ_F_MUTABLE : 0) |
                      (name_len ? VM_OBJ_F_TAGGED : 0) | (h->f.retentive ? VM_OBJ_F_RETENTIVE : 0) |
                      (h->f.dynamic ? VM_OBJ_F_DYNAMIC : 0) | (h->f.usr_protected ? VM_OBJ_F_USR_PROTECTED : 0));
  return w;
}

// ===========================================================================
// SET_DATA (also the telemetry value record)
// ===========================================================================

typedef struct __packed {
  uint16_t id;
  uint16_t start_idx;
  uint16_t byte_len;
} vm_wire_data_t;
_Static_assert(sizeof(vm_wire_data_t) == 6, "SET_DATA record head");

// ===========================================================================
// ADD_ACCESSORS and their index steps
// ===========================================================================

typedef struct __packed {
  uint16_t acc_id;
  uint16_t root_obj_id;
  uint8_t  idx_count;
  uint8_t  idx_len;
} vm_wire_acc_t;
_Static_assert(sizeof(vm_wire_acc_t) == 6, "ADD_ACCESSORS record head");

typedef struct __packed {
  uint8_t  kind;
  uint32_t value;
} vm_wire_idx_literal_t;
_Static_assert(sizeof(vm_wire_idx_literal_t) == 5, "literal step");

typedef struct __packed {
  uint8_t  kind;
  uint16_t acc_id;
} vm_wire_idx_ref_t;
_Static_assert(sizeof(vm_wire_idx_ref_t) == 3, "reference step");

typedef struct __packed {
  uint8_t kind;
  uint8_t name_len;
} vm_wire_idx_name_t;
_Static_assert(sizeof(vm_wire_idx_name_t) == 2, "name step");

// ===========================================================================
// ADD_BLOCK
// ===========================================================================

typedef struct __packed {
  uint16_t blk_id;
  uint16_t block_idx;
  uint8_t  block_type;
  uint8_t  in_cnt;
  uint8_t  q_cnt;
  uint8_t  en_cnt;
  uint8_t  en_mode;
  uint8_t  on_error;
  uint16_t custom_len;
  uint16_t eno_obj_id;
} vm_wire_block_t;
_Static_assert(sizeof(vm_wire_block_t) == 14, "ADD_BLOCK fixed part");

// ===========================================================================
// SUBSCRIBE, EXEC, SPEED, STATUS
// ===========================================================================

typedef struct __packed {
  uint16_t id;
} vm_wire_sub_t;
_Static_assert(sizeof(vm_wire_sub_t) == 2, "SUBSCRIBE record");

typedef struct __packed {
  uint8_t command;  /**< vm_exec_command_e */
} vm_wire_exec_t;
_Static_assert(sizeof(vm_wire_exec_t) == 1, "EXEC body");

typedef struct __packed {
  uint16_t factor;   /**< Slow-motion factor (1 is normal speed) */
  uint16_t step_ms;  /**< Program time a stepped pass adds, ms (0: time stands still while stepping); a 2-byte body (the factor only) leaves it as it is */
} vm_wire_speed_t;
_Static_assert(sizeof(vm_wire_speed_t) == 4, "SPEED body");

/* The answer of STATUS: what the VM is doing and what it holds. */
typedef struct __packed {
  uint8_t mode;             /**< vm_run_mode_e */
  uint8_t load_state;       /**< vm_load_state_e */
  uint8_t ready;            /**< 1: a program is loaded and every slot OPEN declared was uploaded (EXEC can start it) */
  uint8_t fault_latched;    /**< 1: a critical device fault latched; every start is refused until EXEC ACK_FAULT */
  uint16_t fault_device;    /**< the device of the latched fault; meaningful only while fault_latched */
  uint16_t slowdown;        /**< slow-motion factor (1 is normal speed) */
  uint32_t fault_root_tag;  /**< the root cause of the latched fault (an ERR_TAG value, 0 if not known); only while latched */
  uint32_t pass_count;      /**< passes completed since the statistics were last reset */
  uint32_t last_pass_us;    /**< duration of the last pass */
  uint32_t arena_used;      /**< bytes of the program arena in use */
  uint16_t next_block;      /**< the block the next NEXT runs (block mode: the one the VM is parked before, or 0 between two passes); 0xFFFF otherwise */
} vm_wire_status_t;
_Static_assert(sizeof(vm_wire_status_t) == 26, "STATUS answer");
