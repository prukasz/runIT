#include "vm_obj_build.h"
#include "vm_err.h"


/*
 * VM Object & Accessor Construction Engine
 *
 * Logic Flow:
 *   1. Object Construction:
 *      - Shape validation & byte sizing (vm_obj_shape)
 *      - Object header & name initialization (vm_obj_init)
 *      - Object bump-allocation & registry bind (vm_obj_create)
 *   2. Accessor Construction & Caching:
 *      - Accessor allocation & registry bind (vm_accessor_create)
 *      - Index slot helper (index_slot)
 *      - Accessor index setters (vm_accessor_set_literal, vm_accessor_set_ref, vm_accessor_set_name)
 *      - Accessor check and fixed-path resolution (vm_accessor_bind)
 */

// ===========================================================================
// 1. Object Construction
// ===========================================================================

err_h vm_obj_shape(const vm_obj_head_t* head, uint32_t* out_total) {
  ERR_CHECK(head != NULL, ERR_VM_BAD_ARG);
  ERR_CHECK(out_total != NULL, ERR_VM_BAD_ARG);
  *out_total = 0;

  vm_obj_t_e type = (vm_obj_t_e)head->d.obj_t;
  uint8_t w = vm_type_width(type);
  if (type == VM_OBJ_NONE || w == 0) {
    return ERR_NEW(ERR_VM_OBJ_BAD_TYPE, .type = (uint8_t)type);
  }

  // Object must have non-zero payload storage
  if (head->payload_size == 0) {
    return ERR_NEW(ERR_VM_OBJ_EMPTY, .type = (uint8_t)type);
  }

  // Payload size must align to element width (powers of two)
  if (head->payload_size & (uint16_t)(w - 1)) {
    return ERR_NEW(ERR_VM_OBJ_BAD_SIZE, .type = (uint8_t)type, .payload_size = head->payload_size, .width = w);
  }

  // Pointer objects cannot be retentive (pointers would dangle across reboots)
  if (head->f.retentive && type == VM_OBJ_PTR) {
    return ERR_NEW(ERR_VM_OBJ_RETENTIVE_PTR, .type = (uint8_t)type);
  }

  // Note: head->d.name_size is 4 bits, bounded to VM_OBJ_NAME_MAX (15)
  *out_total = (uint32_t)sizeof(vm_obj_head_t) + head->payload_size + head->d.name_size;
  return NULL;
}

void vm_obj_init(vm_obj_h o, const vm_obj_head_t* head, const char* name) {
  o->head = *head;
  o->head.f._unused = 0;
  o->head.f.tagged = head->d.name_size ? 1 : 0;
  o->head.f.dynamic = 0;
  // usr_protected stays as uploaded: the program marks block-owned objects, a user variable a block drives stays unmarked.
  // dynamic is never taken from the wire.

  if (head->d.name_size && name) {
    memcpy(o->payload + head->payload_size, name, head->d.name_size);
  }
}

err_h vm_obj_create(vm_obj_h* out, uint16_t id, const vm_obj_head_t* head, const char* name) {
  ERR_CHECK(out != NULL, ERR_VM_BAD_ARG);
  *out = NULL;

  uint32_t total = 0;
  ERR_TRY(vm_obj_shape(head, &total));

  vm_obj_h o = NULL;
  ERR_TRY(vm_store_alloc((void**)&o, VM_REG_OBJ, id, total));

  vm_obj_init(o, head, name);
  *out = o;
  return NULL;
}

// ===========================================================================
// 2. Accessor Construction & Caching
// ===========================================================================

err_h vm_accessor_create(vm_accessor_t** out, uint16_t id, uint16_t root_obj_id, uint8_t idx_count) {
  ERR_CHECK(out != NULL, ERR_VM_BAD_ARG);
  *out = NULL;

  // Single allocation holds accessor struct and its contiguous index array
  uint32_t total = (uint32_t)sizeof(vm_accessor_t) + (uint32_t)idx_count * sizeof(vm_index_t);
  vm_accessor_t* acc = NULL;
  ERR_TRY(vm_store_alloc((void**)&acc, VM_REG_ACC, id, total));

  acc->id = root_obj_id;
  acc->count = idx_count;
  acc->indices = idx_count ? (vm_index_t*)((uint8_t*)acc + sizeof(vm_accessor_t)) : NULL;

  *out = acc;
  return NULL;
}

static inline MUST_USE err_h index_slot(vm_accessor_t* acc, uint8_t pos, vm_index_t** out) {
  ERR_CHECK(acc != NULL, ERR_VM_BAD_ARG);
  if (pos >= acc->count || acc->indices == NULL) {
    return ERR_NEW(ERR_VM_ACC_INDEX_OOB, .acc_id = acc->id, .pos = pos, .count = acc->count);
  }
  *out = (vm_index_t*)&acc->indices[pos];
  return NULL;
}

err_h vm_accessor_set_literal(vm_accessor_t* acc, uint8_t pos, uint32_t value) {
  vm_index_t* slot = NULL;
  ERR_TRY(index_slot(acc, pos, &slot));
  slot->kind = VM_IDX_LITERAL;
  slot->value = value;
  return NULL;
}

err_h vm_accessor_set_ref(vm_accessor_t* acc, uint8_t pos, const vm_accessor_t* ref) {
  ERR_CHECK(ref != NULL, ERR_VM_BAD_ARG);
  vm_index_t* slot = NULL;
  ERR_TRY(index_slot(acc, pos, &slot));
  slot->kind = VM_IDX_REF;
  slot->ref = ref;
  return NULL;
}

err_h vm_accessor_set_name(vm_accessor_t* acc, uint8_t pos, const char* name, uint8_t name_len) {
  ERR_CHECK(name != NULL, ERR_VM_BAD_ARG);
  if (name_len > VM_OBJ_NAME_MAX) {
    return ERR_NEW(ERR_VM_OBJ_NAME_TOO_LONG, .len = name_len);
  }

  vm_index_t* slot = NULL;
  ERR_TRY(index_slot(acc, pos, &slot));

  // Name bytes are copied into store NUL-terminated for permanent index matching
  char* copy = NULL;
  ERR_TRY(vm_store_alloc((void**)&copy, VM_REG_ACC, VM_ID_NONE, (uint32_t)name_len + 1u));
  memcpy(copy, name, name_len);
  copy[name_len] = '\0';

  slot->kind = VM_IDX_NAME;
  slot->name_len = name_len;
  slot->name = copy;
  return NULL;
}

err_h vm_accessor_bind(vm_accessor_t* acc) {
  ERR_CHECK(acc != NULL, ERR_VM_BAD_ARG);
  acc->flags &= (uint8_t)~VM_ACC_F_CACHED;

  // Objects load before accessors: a root that isn't there now never will be
  vm_obj_h obj = vm_obj_get_by_id(acc->id);
  if (!obj) return vm_err_unknown_id(acc->id);

  if (acc->count == 0) {
    acc->c_payload = vm_make_payload(obj);  // the whole object
  } else if (acc->count == 1 && acc->indices[0].kind == VM_IDX_LITERAL) {
    // One element of the root. An arena object never moves, so the address holds for the whole load.
    uint8_t* p = vm_obj_get_elem_ptr(obj, acc->indices[0].value);
    if (!p) return vm_err_chain_oob(acc->id, 0, acc->indices[0].value, acc->id);
    acc->c_payload = (vm_obj_payload_t){.ptr = p, .owner = obj, .count = 1, .type = (uint8_t)obj->head.d.obj_t};
  } else {
    // A REF or NAME step, or a step past the root: a PTR cell on the way can be re-linked at run time, so it is walked
    return NULL;
  }
  acc->flags |= VM_ACC_F_CACHED;
  return NULL;
}
