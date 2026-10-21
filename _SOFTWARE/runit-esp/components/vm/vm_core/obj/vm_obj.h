#pragma once

#include "esp_compiler.h"
#include "vm_err.h"

/*
 * VM Object Model & Header Definitions
 *
 * VM object: a head plus a flexible-array payload holding one value or an array
 * of them, plus an optional 15-char tag. No parent pointer -- objects form a
 * tree only through other objects' PTR elements, resolved via accessor + id.
 *
 * Logic Flow:
 *   1. Types & Core Data Structures (Enums, Bitflags, Head & Object structs, assertions)
 *   2. Helpers (Type table lookups, element shifts, width determination, validation)
 *   3. Target APIs (Object constructor vm_make_obj_head, geometry, payload & tag accessors)
 */

// ===========================================================================
// 1. Types & Core Data Structures
// ===========================================================================

// Longest tag a 4-bit name_size can describe (4-bit size in obj head)
#define VM_OBJ_NAME_MAX 15

/**
 * @brief Possible types of object items stored
 */

//#ref-enum @alias VM Data Type
typedef enum vm_obj_t_e {
  VM_OBJ_NONE = 0,  //@alias Empty @description Empty: no payload type
  VM_OBJ_PTR  = 1,  //@alias Object Reference @description Object reference: reference to another VM object (wire: 2B, mem: 4B)
  VM_OBJ_U8   = 2,  //@alias Unsigned 8-bit Integer @description Unsigned 8-bit integer (wire: 1B, mem: 1B)
  VM_OBJ_U32  = 3,  //@alias Unsigned 32-bit Integer @description Unsigned 32-bit integer (wire: 4B, mem: 4B)
  VM_OBJ_I32  = 4,  //@alias Signed 32-bit Integer @description Signed 32-bit integer (wire: 4B, mem: 4B)
  VM_OBJ_F    = 5,  //@alias Float @description 32-bit float (wire: 4B, mem: 4B)
  VM_OBJ_B    = 6,  //@alias Boolean @description Boolean (wire: 1B, mem: 1B)
  VM_OBJ_STR  = 7,  //@alias Text @description Text string (wire: 1B, mem: 1B)
} vm_obj_t_e;

/* The object's flags: the bits of vm_obj_head_t.f in its order, and the flag byte of the wire head (vm_wire.h). The
 * app sets mutable, upd_resetable, retentive and usr_protected; tagged and dynamic are the board's. */
//#ref-defines vm_obj_flag @alias Object Flags @prefix VM_OBJ_F_
#define VM_OBJ_F_MUTABLE       (1u << 0)  //@alias Writable @description Writable at run time
#define VM_OBJ_F_UPD_RESETABLE (1u << 2)  //@alias Fresh-tracked @description Freshness cleared at end of pass
#define VM_OBJ_F_TAGGED        (1u << 3)  //@alias Named by the board @description Named by the board
#define VM_OBJ_F_RETENTIVE     (1u << 4)  //@alias Retained @description Retained after restart
#define VM_OBJ_F_DYNAMIC       (1u << 5)  //@alias Heap object @description Heap object
#define VM_OBJ_F_USR_PROTECTED (1u << 6)  //@alias User-protected @description User runtime writes refused
// Bit 1 is `upd` (runtime state) and bit 7 is unused: on the wire both are sent as 0 and ignored when read.

/* The head is four bytes on the wire too, but in an explicit layout (vm_wire_head_t, vm_wire.h), not as the
 * compiler lays out vm_obj_head_t. */
#define VM_OBJ_HEAD_WIRE_SIZE 4u

/* A PTR element is a 4-byte pointer in memory and its child's u16 object ID
 * on the wire (SET_DATA records, both directions). */
#define VM_OBJ_PTR_WIRE_SIZE 2u

/**
 * @brief Object head describing every existing object.
 */

typedef struct __attribute__((aligned(4))) vm_obj_head_t {
  uint16_t payload_size;
  struct {
    uint8_t obj_t     : 4;
    uint8_t name_size : 4;
  } d;
  struct {
    uint8_t mutable       : 1;
    uint8_t upd           : 1;
    uint8_t upd_resetable : 1;
    uint8_t tagged        : 1;
    uint8_t retentive     : 1;
    uint8_t dynamic       : 1;
    uint8_t usr_protected : 1;
    uint8_t _pad          : 1;
  } f;
} vm_obj_head_t;

_Static_assert(sizeof(vm_obj_head_t) == 4, "vm_obj_head_t must stay 4 bytes");

/**
 * @brief VM object consisting of head descriptor followed by flexible-array payload.
 */
typedef struct vm_obj_t {
  vm_obj_head_t head;
  uint8_t       payload[];
} vm_obj_t;

/**
 * @brief Object handle used across all files
 */
typedef vm_obj_t* vm_obj_h;

_Static_assert(offsetof(vm_obj_t, payload) == 4, "payload must follow the head with no padding");

// ===========================================================================
// 2. Helpers (Type Shifts, Widths & Validation)
// ===========================================================================

/* log2 of an element's width in memory, indexed by the whole 4-bit obj_t field (NONE and unused encodings: 0). */
static const uint8_t vm_obj_type_shifts[16] = {
    [VM_OBJ_PTR] = 2,  // a pointer
    [VM_OBJ_U32] = 2,
    [VM_OBJ_I32] = 2,
    [VM_OBJ_F]   = 2,
};

_Static_assert(sizeof(void*) == 4, "vm_obj_type_shifts assumes 4-byte pointers");

// A real type: PTR to STR.
static inline bool vm_type_ok(uint8_t t) {
  return t >= VM_OBJ_PTR && t <= VM_OBJ_STR;
}

// A stored scalar: U8 to STR (not PTR).
static inline bool vm_type_is_scalar(uint8_t t) {
  return t >= VM_OBJ_U8 && t <= VM_OBJ_STR;
}

static inline uint32_t vm_type_shift(uint8_t t) {
  return vm_obj_type_shifts[t & 0x0Fu];
}

// Bytes of one element in memory; 0 for a type that is not real.
static inline uint8_t vm_type_width(vm_obj_t_e t) {
  return vm_type_ok(t) ? (uint8_t)(1u << vm_type_shift(t)) : 0;
}

// ===========================================================================
// 3. Target APIs (Object Inspection & Head Construction)
// ===========================================================================

// A head for `count` elements of `type`; the name is clipped to VM_OBJ_NAME_MAX.
static inline vm_obj_head_t vm_make_obj_head(vm_obj_t_e type, uint16_t count, uint8_t flags, uint8_t name_len) {
  return (vm_obj_head_t){
      .payload_size = (uint16_t)(count << vm_type_shift(type)),
      .d            = {.obj_t = type, .name_size = name_len > VM_OBJ_NAME_MAX ? VM_OBJ_NAME_MAX : name_len},
      .f            = {.mutable       = !!(flags & VM_OBJ_F_MUTABLE),
                       .upd_resetable = !!(flags & VM_OBJ_F_UPD_RESETABLE),
                       .retentive     = !!(flags & VM_OBJ_F_RETENTIVE),
                       .usr_protected = !!(flags & VM_OBJ_F_USR_PROTECTED)},
  };
}

static inline uint16_t vm_obj_get_payload_size(vm_obj_h obj) {
  return obj->head.payload_size;
}

// Bytes of one element; 0 for a type that is not real.
static inline uint8_t vm_obj_get_type_size(vm_obj_h obj) {
  return vm_type_width(obj->head.d.obj_t);
}

// Elements in the payload; 0 for a type that is not real.
static inline uint16_t vm_obj_get_items_cnt(vm_obj_h obj) {
  return vm_type_ok(obj->head.d.obj_t) ? (uint16_t)(obj->head.payload_size >> vm_type_shift(obj->head.d.obj_t)) : 0;
}

// Head, payload and tag.
static inline uint32_t vm_obj_get_total_size(vm_obj_h obj) {
  return (uint32_t)sizeof(vm_obj_head_t) + obj->head.payload_size + obj->head.d.name_size;
}

// The payload; NULL when it is empty.
static inline uint8_t* vm_obj_get_payload_ptr(vm_obj_h obj) {
  return obj->head.payload_size != 0 ? obj->payload : NULL;
}

// Address of element `i`; NULL past the end or for a type that is not real.
static inline uint8_t* vm_obj_get_elem_ptr(vm_obj_h obj, uint32_t i) {
  const uint8_t t = obj->head.d.obj_t;
  if (unlikely(!vm_type_ok(t) || i > UINT16_MAX)) return NULL;
  const uint32_t off = i << vm_type_shift(t);
  return likely(off < obj->head.payload_size) ? obj->payload + off : NULL;
}

// The tag and its length in *out_len; NULL for an object without a name.
static inline const char* vm_obj_get_tag(vm_obj_h obj, uint8_t* out_len) {
  if (!obj->head.f.tagged) return NULL;
  *out_len = obj->head.d.name_size;
  return (const char*)(obj->payload + obj->head.payload_size);
}
