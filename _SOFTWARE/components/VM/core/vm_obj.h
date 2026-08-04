#pragma once
#include <stdint.h>
#include <sys/cdefs.h>

typedef enum vm_obj_t_e {
  VM_OBJ_NONE = 0,
  VM_OBJ_PTR = 1,  // ptr at object
  VM_OBJ_U8 = 2,
  VM_OBJ_U32 = 3,
  VM_OBJ_I32 = 4,
  VM_OBJ_F = 5,
  VM_OBJ_B = 6,
  VM_OBJ_STR = 7,
  VM_OBJ_U64 = 8,
} vm_obj_t_e;

static const uint8_t vm_obj_type_sizes[] = {
    [VM_OBJ_NONE] = 0,
    [VM_OBJ_PTR] = sizeof(void*),
    [VM_OBJ_U8] = sizeof(uint8_t),
    [VM_OBJ_U32] = sizeof(uint32_t),
    [VM_OBJ_I32] = sizeof(int32_t),
    [VM_OBJ_F] = sizeof(float),
    [VM_OBJ_B] = sizeof(uint8_t),
    [VM_OBJ_STR] = sizeof(uint8_t),
    [VM_OBJ_U64] = sizeof(uint64_t),
};

/**
  Multi dimensional array is created as array of objects containing array
 */

typedef struct __packed vm_obj_head_t {
  uint16_t payload_size;  // value length in bytes -- payload[0..payload_size) holds a single value or an array, data_size = N * type_size for an array
  struct {
    uint8_t obj_t : 4;      // what type is stored
    uint8_t name_size : 4;  // up to 15 chars, payload[payload_size]+name[name_size]
  } d;
  struct {
    uint8_t mutable : 1;        // is value editable by any one
    uint8_t usr_mutable : 1;    // is editable by user-code
    uint8_t upd : 1;            // has value been updated / refreshed lately
    uint8_t upd_resetable : 1;  // can flag be reset
    uint8_t tagged : 1;         // is name field populated
    uint8_t retentive : 1;      // should be stored in nvs - requires type of non-prt
  } f;
} vm_obj_head_t;

_Static_assert(sizeof(vm_obj_head_t) == 4, "vm_obj_head_t must stay 4 bytes");

/**
 * @brief vm_object consisit of head - always present object descriptor and flexible array memeber
 * data with declared size in head - it stores object body
 */
typedef struct __packed vm_obj_t {
  vm_obj_head_t head;
  uint8_t payload[];
} vm_obj_t;

typedef vm_obj_t* vm_obj_h;

static __always_inline uint32_t vm_obj_total_size(vm_obj_h obj) {
  return (uint32_t)sizeof(vm_obj_head_t) + obj->head.payload_size + obj->head.d.name_size;
}

static __always_inline uint16_t vm_obj_payload_size(vm_obj_h obj) {
  return obj->head.payload_size;
}

static __always_inline uint8_t vm_obj_type_size(vm_obj_h obj) {
  uint8_t t = obj->head.d.obj_t;
  return (t < sizeof(vm_obj_type_sizes) / sizeof(vm_obj_type_sizes[0])) ? vm_obj_type_sizes[t] : 0;
}

static __always_inline uint16_t vm_obj_items_cnt(vm_obj_h obj) {
  uint8_t obj_type_size = vm_obj_type_size(obj);
  return obj_type_size != 0 ? vm_obj_payload_size(obj) / obj_type_size : 0;
}

static __always_inline const char* vm_obj_tag(vm_obj_h obj, uint8_t* out_len) {
  if (!obj->head.f.tagged) return NULL;
  *out_len = obj->head.d.name_size;
  return (const char*)(obj->payload + obj->head.payload_size);
}

static __always_inline uint8_t* vm_obj_payload(vm_obj_h obj) {
  return obj->head.payload_size != 0 ? obj->payload : NULL;
}
