#include "vm_obj_build.h"
#include <string.h>

#define OWNER OWNER_VM_OBJ

err_h vm_obj_create(vm_obj_h* out, vm_alloc_t* arena, const vm_obj_cfg_t* cfg) {
  SE_CHECK_NOT_NULL(out);
  SE_CHECK_NOT_NULL(arena);
  SE_CHECK_NOT_NULL(cfg);
  *out = NULL;

  uint8_t w = vm_type_width(cfg->type);
  if (cfg->type == VM_OBJ_NONE || w == 0) {
    SE_RET_ERR(ERR_VM_OBJ_BAD_TYPE, .type = (uint8_t)cfg->type);
  }

  /* A zero-element object has no storage, yet it still has an address: every
     resolve path would hand back a pointer to where its payload *would*
     start, which is the next allocation's first byte. A read there returns a
     neighbour's value and a write corrupts it, silently in both directions.
     item_count comes straight off the wire, so reject the shape once here
     rather than defend every read and write against it. */
  if (cfg->item_count == 0) {
    SE_RET_ERR(ERR_VM_OBJ_EMPTY, .type = (uint8_t)cfg->type);
  }

  // computed in 32 bits on purpose -- the point is to catch the overflow that
  // truncating into payload_size would otherwise hide
  uint32_t bytes = (uint32_t)cfg->item_count * w;
  if (bytes > UINT16_MAX) {
    SE_RET_ERR(ERR_VM_OBJ_TOO_LARGE, .type = (uint8_t)cfg->type, .item_count = cfg->item_count, .bytes = bytes);
  }

  size_t nlen = cfg->name ? strlen(cfg->name) : 0;
  if (nlen > VM_OBJ_NAME_MAX) {
    SE_RET_ERR(ERR_VM_OBJ_NAME_TOO_LONG, .len = (uint8_t)(nlen > 255 ? 255 : nlen));
  }

  /* A retentive object's payload is written to NVS verbatim and read back on
     the next boot. Pointer payloads are addresses into this boot's arena, so
     restoring them would install dangling handles -- reject at creation
     rather than discovering it during a flush. */
  if (cfg->retentive && cfg->type == VM_OBJ_PTR) {
    SE_RET_ERR(ERR_VM_OBJ_RETENTIVE_PTR, .type = (uint8_t)cfg->type);
  }

  uint32_t total = (uint32_t)sizeof(vm_obj_head_t) + bytes + (uint32_t)nlen;
  vm_obj_h o = (vm_obj_h)vm_alloc4(arena, total);
  if (!o) {
    // vm_alloc() already reported requested-vs-remaining; this just propagates
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }

  // the arena hands back whatever the previous program left there
  memset(o, 0, total);

  o->head.payload_size = (uint16_t)bytes;
  o->head.d.obj_t = (uint8_t)cfg->type;
  o->head.d.name_size = (uint8_t)nlen;
  o->head.f.mutable = cfg->mutable ? 1 : 0;
  o->head.f.usr_mutable = cfg->usr_mutable ? 1 : 0;
  o->head.f.upd = 0;
  o->head.f.upd_resetable = cfg->upd_resetable ? 1 : 0;
  o->head.f.tagged = nlen ? 1 : 0;
  o->head.f.retentive = cfg->retentive ? 1 : 0;

  if (nlen) memcpy(o->payload + bytes, cfg->name, nlen);

  *out = o;
  return NULL;
}

err_h vm_obj_table_build(vm_obj_table_t* table, vm_alloc_t* arena, uint16_t count) {
  SE_CHECK_NOT_NULL(table);
  SE_CHECK_NOT_NULL(arena);

  table->items = NULL;
  table->count = 0;
  if (count == 0) return NULL;

  vm_obj_h* items = (vm_obj_h*)vm_alloc4(arena, (uint32_t)count * sizeof(vm_obj_h));
  if (!items) {
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }
  memset(items, 0, (size_t)count * sizeof(vm_obj_h));

  table->items = items;
  table->count = count;
  return NULL;
}

err_h vm_obj_table_set(vm_obj_table_t* table, uint16_t id, vm_obj_h obj) {
  SE_CHECK_NOT_NULL(table);
  SE_CHECK_NOT_NULL(obj);

  if (id >= table->count || table->items == NULL) {
    SE_RET_ERR(ERR_VM_OBJ_TABLE_OOB, .id = id, .count = table->count);
  }
  if (table->items[id] != NULL) {
    SE_RET_ERR(ERR_VM_OBJ_TABLE_DUP, .id = id);
  }

  table->items[id] = obj;
  return NULL;
}

void vm_obj_table_reset(vm_obj_table_t* table) {
  if (!table) return;
  /* Only detaches -- the entries themselves live in the arena and go away
     when it is reset, which is the caller's next step. */
  table->items = NULL;
  table->count = 0;
}

/* =========================================================================
   Accessors
   ========================================================================= */

err_h vm_accessor_table_build(vm_accessor_table_t* table, vm_alloc_t* arena, uint16_t count) {
  SE_CHECK_NOT_NULL(table);
  SE_CHECK_NOT_NULL(arena);

  table->items = NULL;
  table->count = 0;
  if (count == 0) return NULL;

  vm_accessor_t** items = (vm_accessor_t**)vm_alloc4(arena, (uint32_t)count * sizeof(vm_accessor_t*));
  if (!items) {
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }
  memset(items, 0, (size_t)count * sizeof(vm_accessor_t*));

  table->items = items;
  table->count = count;
  return NULL;
}

err_h vm_accessor_table_set(vm_accessor_table_t* table, uint16_t id, vm_accessor_t* acc) {
  SE_CHECK_NOT_NULL(table);
  SE_CHECK_NOT_NULL(acc);

  if (id >= table->count || table->items == NULL) {
    SE_RET_ERR(ERR_VM_ACC_TABLE_OOB, .id = id, .count = table->count);
  }
  if (table->items[id] != NULL) {
    SE_RET_ERR(ERR_VM_ACC_TABLE_DUP, .id = id);
  }

  table->items[id] = acc;
  return NULL;
}

void vm_accessor_table_reset(vm_accessor_table_t* table) {
  if (!table) return;
  table->items = NULL;
  table->count = 0;
}

err_h vm_accessor_create(vm_accessor_t** out, vm_alloc_t* arena, uint16_t root_obj_id, uint8_t idx_count) {
  SE_CHECK_NOT_NULL(out);
  SE_CHECK_NOT_NULL(arena);
  *out = NULL;

  /* One slice for the accessor and its index array: they have identical
     lifetimes and are always walked together, so splitting them would only
     add an allocation and a pointer to get wrong. */
  uint32_t total = (uint32_t)sizeof(vm_accessor_t) + (uint32_t)idx_count * sizeof(vm_index_t);
  uint8_t* mem = (uint8_t*)vm_alloc4(arena, total);
  if (!mem) {
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }
  memset(mem, 0, total);

  vm_accessor_t* acc = (vm_accessor_t*)mem;
  acc->id = root_obj_id;
  acc->count = idx_count;
  acc->indices = idx_count ? (vm_index_t*)(mem + sizeof(vm_accessor_t)) : NULL;

  *out = acc;
  return NULL;
}

// indices are const to readers; construction is the one place that writes them
static err_h index_slot(vm_accessor_t* acc, uint8_t pos, vm_index_t** out) {
  SE_CHECK_NOT_NULL(acc);
  if (pos >= acc->count || acc->indices == NULL) {
    SE_RET_ERR(ERR_VM_ACC_INDEX_OOB, .acc_id = acc->id, .pos = pos, .count = acc->count);
  }
  *out = (vm_index_t*)&acc->indices[pos];
  return NULL;
}

err_h vm_accessor_set_literal(vm_accessor_t* acc, uint8_t pos, uint32_t value) {
  vm_index_t* slot = NULL;
  SE_RET_IF_ERR(index_slot(acc, pos, &slot));
  slot->kind = VM_IDX_LITERAL;
  slot->value = value;
  return NULL;
}

err_h vm_accessor_set_ref(vm_accessor_t* acc, uint8_t pos, const vm_accessor_t* ref) {
  SE_CHECK_NOT_NULL(ref);
  vm_index_t* slot = NULL;
  SE_RET_IF_ERR(index_slot(acc, pos, &slot));
  slot->kind = VM_IDX_REF;
  slot->ref = ref;
  return NULL;
}

err_h vm_accessor_set_name(vm_accessor_t* acc, uint8_t pos, vm_alloc_t* arena, const char* name, uint8_t name_len) {
  SE_CHECK_NOT_NULL(arena);
  SE_CHECK_NOT_NULL(name);
  if (name_len > VM_OBJ_NAME_MAX) {
    SE_RET_ERR(ERR_VM_OBJ_NAME_TOO_LONG, .len = name_len);
  }

  vm_index_t* slot = NULL;
  SE_RET_IF_ERR(index_slot(acc, pos, &slot));

  /* The wire form is unterminated and the frame buffer is gone once the
     packet is handled, so the name has to be copied into program storage. */
  char* copy = (char*)vm_alloc4(arena, (uint32_t)name_len + 1u);
  if (!copy) {
    SE_RET_ERR(ERR_BASE_NO_MEM, 0);
  }
  memcpy(copy, name, name_len);
  copy[name_len] = '\0';

  slot->kind = VM_IDX_NAME;
  slot->name_len = name_len;  // measured here so the scan never calls strlen()
  slot->name = copy;
  return NULL;
}

bool vm_accessor_cache_build(vm_accessor_t* acc) {
  if (!acc) return false;

  /* Cleared first: rebuilding an accessor whose object went away, or whose
     shape stopped qualifying, must drop the old entry rather than leave a
     stale address behind a set flag. */
  acc->flags &= (uint8_t)~VM_ACC_F_CACHED;

  if (acc->count > 1) return false;  // deeper chains cross a link -- see the header

  vm_obj_h obj = vm_obj_by_id(acc->id);
  if (!obj) return false;  // accessor built before its object; stays uncached

  if (acc->count == 0) {
    vm_payload_t p = vm_obj_as_payload(obj);
    acc->c_ptr = p.ptr;
    acc->c_type = (uint8_t)p.type;
    acc->c_count = p.count;
  } else {
    if (acc->indices[0].kind != VM_IDX_LITERAL) return false;
    uint8_t* p = vm_obj_elem_ptr(obj, acc->indices[0].value);
    if (!p) return false;  // out of range -- leave it to report properly at access
    acc->c_ptr = p;
    acc->c_type = obj->head.d.obj_t;
    acc->c_count = 1;
  }

  acc->c_owner = obj;
  acc->flags |= VM_ACC_F_CACHED;
  return true;
}
