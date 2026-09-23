#pragma once

#include <stdint.h>
#include "esp_compiler.h"

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
#define VM_OBJ_NAME_MAX 15  //@vm-constant @description Longest object or accessor name, in bytes.

/**
 * @brief Possible types of object items stored
 */

//#ref-enum @alias VM Data Type
typedef enum vm_obj_t_e {
  VM_OBJ_NONE = 0,  //@alias Empty @description No payload type.
  VM_OBJ_PTR  = 1,  //@alias Object Reference @description A reference to another VM object.
  VM_OBJ_U8   = 2,  //@alias Unsigned 8-bit Integer
  VM_OBJ_U32  = 3,  //@alias Unsigned 32-bit Integer
  VM_OBJ_I32  = 4,  //@alias Signed 32-bit Integer
  VM_OBJ_F    = 5,  //@alias Float
  VM_OBJ_B    = 6,  //@alias Boolean
  VM_OBJ_STR  = 7,  //@alias Text
} vm_obj_t_e;


/**
 * @brief Object creation and descriptor flags.
 */
#define VM_OBJ_F_MUTABLE       (1u << 0)
#define VM_OBJ_F_UPD_RESETABLE (1u << 1)
#define VM_OBJ_F_RETENTIVE     (1u << 2)
#define VM_OBJ_F_USR_PROTECTED (1u << 3)

/* ESP32 GCC loader ABI: vm_obj_head_t is transmitted as its exact four-byte
 * in-memory representation. TypeScript mirrors this target-specific layout. */
#define VM_OBJ_HEAD_WIRE_SIZE 4u  //@vm-constant @description Bytes of vm_obj_head_t on the wire.

/* A PTR element is a 4-byte pointer in memory and its child's u16 object ID
 * on the wire (0x43 records, both directions). */
#define VM_OBJ_PTR_WIRE_SIZE 2u  //@vm-constant @description Bytes of one PTR element on the wire (a child object ID).

/**
 * @brief Object head describing every existing object.
 */

//#vm-struct-ref @alias VM Object Header @kind vm-object-header @wire-abi esp32-gcc-bitfield-v1 @wire-size 4
typedef struct __attribute__((aligned(4))) vm_obj_head_t {
  uint16_t payload_size;  //@alias Payload Size @unit bytes @role payload-size @wire-offset 0 @wire-type u16-le
  struct {
    uint8_t obj_t     : 4;  //@alias Data Type @role object-type @enum-ref vm_obj_t_e @one-of [$VM_OBJ_NONE, $VM_OBJ_PTR, $VM_OBJ_U8, $VM_OBJ_U32, $VM_OBJ_I32, $VM_OBJ_F, $VM_OBJ_B, $VM_OBJ_STR] @wire-offset 2 @wire-bit-offset 0
    uint8_t name_size : 4;  //@alias Name Length @unit chars @role name-size @min 0 @max VM_OBJ_NAME_MAX @wire-offset 2 @wire-bit-offset 4
  } d;  //@group descriptor @wire-offset 2
  struct {
    uint8_t mutable       : 1;  //@alias Mutable @role mutable @wire-offset 3 @wire-bit-offset 0
    uint8_t upd           : 1;  //@alias Updated @role updated @wire-offset 3 @wire-bit-offset 1 @device-sets 0 at load
    uint8_t upd_resetable : 1;  //@alias Resettable Update Flag @role update-resettable @wire-offset 3 @wire-bit-offset 2
    uint8_t tagged        : 1;  //@alias Has Name @role has-name @wire-offset 3 @wire-bit-offset 3 @device-sets name_size != 0
    uint8_t retentive     : 1;  //@alias Retentive @role retentive @note Not valid for pointer objects. @wire-offset 3 @wire-bit-offset 4
    uint8_t dynamic       : 1;  //@alias Dynamic @role dynamic @note Runtime heap allocation. @wire-offset 3 @wire-bit-offset 5 @device-sets 0 at load; 1 on heap objects
    uint8_t usr_protected : 1;  //@alias User Protected @role user-protected @wire-offset 3 @wire-bit-offset 6 @device-sets 1 on block outputs and ENO objects
    uint8_t _pad          : 1;  //@internal @wire-offset 3 @wire-bit-offset 7
  } f;  //@group flags @wire-offset 3
} vm_obj_head_t;

_Static_assert(sizeof(vm_obj_head_t) == 4, "vm_obj_head_t must stay 4 bytes");

/**
 * @brief VM object consisting of head descriptor followed by flexible-array payload.
 */
//#vm-struct-ref @alias VM Object @kind vm-object @variable-size
typedef struct vm_obj_t {
  vm_obj_head_t head;       //@alias Header @role header
  uint8_t       payload[];  //@alias Payload @role payload @element-type-from d.obj_t @length-from payload_size
} vm_obj_t;

/**
 * @brief Object handle used across all files
 */
typedef vm_obj_t* vm_obj_h;

_Static_assert(offsetof(vm_obj_t, payload) == 4, "payload must follow the head with no padding");

// ===========================================================================
// 2. Helpers (Type Shifts, Widths & Validation)
// ===========================================================================

/**
 * @brief Size of type lookup table
 */
static const uint8_t vm_obj_type_sizes[] = {
    [VM_OBJ_NONE] = 0,
    [VM_OBJ_PTR]  = sizeof(void*),
    [VM_OBJ_U8]   = sizeof(uint8_t),
    [VM_OBJ_U32]  = sizeof(uint32_t),
    [VM_OBJ_I32]  = sizeof(int32_t),
    [VM_OBJ_F]    = sizeof(float),
    [VM_OBJ_B]    = sizeof(uint8_t),
    [VM_OBJ_STR]  = sizeof(uint8_t),
};

/** @brief Shift to get payload item count from payload size, indexed by the
 *  full 4-bit obj_t field; unused encodings read as 0. */
static const uint8_t vm_obj_type_shifts[16] = {
    [VM_OBJ_NONE] = 0,
    [VM_OBJ_PTR]  = 2,
    [VM_OBJ_U8]   = 0,
    [VM_OBJ_U32]  = 2,
    [VM_OBJ_I32]  = 2,
    [VM_OBJ_F]    = 2,
    [VM_OBJ_B]    = 0,
    [VM_OBJ_STR]  = 0,
};

_Static_assert(sizeof(void*) == 4, "vm_obj_type_shifts assumes 4-byte pointers");

/** @brief Element-size shift for a type id; 0 if `t` is not a real type. */
static __always_inline uint32_t vm_type_shift(uint8_t t) {
  return vm_obj_type_shifts[t & 0x0Fu];
}

/** @brief True if `t` is a real type: VM_OBJ_PTR..VM_OBJ_STR. */
static __always_inline bool vm_type_ok(uint8_t t) {
  return (uint8_t)(t - 1u) <= (uint8_t)(VM_OBJ_STR - 1u);
}

/** @brief Byte width of an element of type `t`. */
static __always_inline uint8_t vm_type_width(vm_obj_t_e t) {
  return ((uint8_t)t < sizeof(vm_obj_type_sizes) / sizeof(vm_obj_type_sizes[0])) ? vm_obj_type_sizes[t] : 0;
}

/** @brief True if `t` is a stored scalar type: VM_OBJ_U8..VM_OBJ_STR. */
static __always_inline bool vm_type_is_scalar(uint8_t t) {
  return (uint8_t)(t - VM_OBJ_U8) <= (uint8_t)(VM_OBJ_STR - VM_OBJ_U8);
}

// ===========================================================================
// 3. Target APIs (Object Inspection & Head Construction)
// ===========================================================================

/**
 * @brief Construct a vm_obj_head_t descriptor from parameters.
 *
 * Automatically computes payload_size from item_count and type width.
 *
 * @param type vm_obj_t_e element type (e.g. VM_OBJ_F, VM_OBJ_U32, VM_OBJ_PTR).
 * @param item_count Number of elements (scalar is 1).
 * @param flags Flag bits (VM_OBJ_F_MUTABLE, etc.).
 * @param name_len Length of tag (0..15).
 */
static __always_inline vm_obj_head_t vm_make_obj_head(vm_obj_t_e type, uint16_t item_count, uint8_t flags, uint8_t name_len) {
  vm_obj_head_t h   = {0};
  h.payload_size    = (uint16_t)(item_count << vm_type_shift((uint8_t)type));
  h.d.obj_t         = (uint8_t)type;
  h.d.name_size     = (uint8_t)(name_len > VM_OBJ_NAME_MAX ? VM_OBJ_NAME_MAX : name_len);
  h.f.mutable       = (flags & VM_OBJ_F_MUTABLE) != 0;
  h.f.upd_resetable = (flags & VM_OBJ_F_UPD_RESETABLE) != 0;
  h.f.retentive     = (flags & VM_OBJ_F_RETENTIVE) != 0;
  h.f.usr_protected = (flags & VM_OBJ_F_USR_PROTECTED) != 0;
  return h;
}

/**
 * @brief Payload size getter
 */
static __always_inline uint16_t vm_obj_get_payload_size(vm_obj_h obj) {
  return obj->head.payload_size;
}

/**
 * @brief Safe object item size getter
 */
static __always_inline uint8_t vm_obj_get_type_size(vm_obj_h obj) {
  return vm_type_width((vm_obj_t_e)obj->head.d.obj_t);
}

/**
 * @brief Total items count calculated from payload size and object type size
 */
static __always_inline uint16_t vm_obj_get_items_cnt(vm_obj_h obj) {
  uint8_t t = obj->head.d.obj_t;
  if (unlikely(!vm_type_ok(t))) return 0;
  return (uint16_t)(vm_obj_get_payload_size(obj) >> vm_type_shift(t));
}

/**
 * @brief Total size that is allocated, including header, payload, and tag
 */
static __always_inline uint32_t vm_obj_get_total_size(vm_obj_h obj) {
  return (uint32_t)sizeof(vm_obj_head_t) + obj->head.payload_size + obj->head.d.name_size;
}

/**
 * @brief Payload getter as uint8_t ptr
 */
static __always_inline uint8_t* vm_obj_get_payload_ptr(vm_obj_h obj) {
  return obj->head.payload_size != 0 ? obj->payload : NULL;
}

/**
 * @brief Address of element `i`, or NULL if `i` is past the end, `t` is not a
 *        real type, or `i` exceeds UINT16_MAX.
 */
static __always_inline uint8_t* vm_obj_get_elem_ptr(vm_obj_h obj, uint32_t i) {
  uint8_t t = obj->head.d.obj_t;
  if (unlikely(!vm_type_ok(t) || i > UINT16_MAX)) return NULL;
  uint32_t off = i << vm_type_shift(t);
  if (unlikely(off >= obj->head.payload_size)) return NULL;
  return obj->payload + off;
}

/**
 * @brief Tag getter
 */
static __always_inline const char* vm_obj_get_tag(vm_obj_h obj, uint8_t* out_len) {
  if (!obj->head.f.tagged) return NULL;
  *out_len = obj->head.d.name_size;
  return (const char*)(obj->payload + obj->head.payload_size);
}
