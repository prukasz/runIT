#pragma once
#include <stdint.h>

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

typedef enum vm_obj_f_e {
  VM_OBJ_F_NONE = 0,
  VM_OBJ_F_EMBEDDED = 1,  // single value embedded
  VM_OBJ_F_ARRAY = 2,     // embedded in body array of (obj_t_e)
} vm_obj_f_e;

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
  uint16_t size;  // sizeof(data), lets the arena be walked without a type switch
  struct {
    uint8_t obj_t : 4;
    uint8_t obj_f : 4;
  } d;
  struct {
    uint8_t mutable : 1;        // is value editable by any one
    uint8_t usr_mutable : 1;    // is editable by user-code
    uint8_t upd : 1;            // has value been updated / refreshed - for vm chain
    uint8_t upd_resetable : 1;  // can flag be reset
    uint8_t subscribed : 1;     // is var being subscribed to (sent remotely)
    uint8_t tagged : 1;         // data is prefixed with [name_len: u8][name bytes],padded to 4
    uint8_t retentive : 1;      // should be stored in nvs
  } f;
} vm_obj_head_t;

typedef struct __packed vm_obj_t {
  vm_obj_head_t head;
  uint8_t data[];
} vm_obj_t;
