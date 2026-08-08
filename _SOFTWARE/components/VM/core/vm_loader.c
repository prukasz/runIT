#include "vm_loader.h"
#include <string.h>

#define OWNER OWNER_VM_LOADER

static uint8_t s_pool[VM_LOADER_POOL_SIZE];
static vm_alloc_t s_arena;
static vm_load_state_e s_state = VM_LOAD_EMPTY;

static err_h require_state(vm_load_state_e want) {
  if (s_state != want) {
    SE_RET_ERR(ERR_VM_LOAD_BAD_STATE, .state = (uint8_t)s_state, .expected = (uint8_t)want);
  }
  return NULL;
}

void vm_loader_reset(void) {
  /* Detach the tables before resetting the arena they point into: for the
     instant in between, an accessor or a late callback resolving an id must
     find NULL rather than storage that is about to be handed out again. */
  vm_obj_table_reset(&g_vm_obj_table);
  vm_accessor_table_reset(&g_vm_accessor_table);
  vm_alloc_init(&s_arena, s_pool, sizeof(s_pool));
  s_state = VM_LOAD_EMPTY;
}

err_h vm_loader_open(uint16_t obj_cnt, uint16_t acc_cnt, uint32_t total_size) {
  vm_loader_reset();

  if (total_size > sizeof(s_pool)) {
    SE_RET_ERR(ERR_VM_LOAD_TOO_BIG, .requested = total_size, .available = (uint32_t)sizeof(s_pool));
  }

  /* Cap the arena at what the program declared, not at the pool. A program
     that under-declares then overruns fails on the object that does it,
     which points at the bug, instead of succeeding on borrowed space and
     failing later for an unrelated reason. */
  vm_alloc_init(&s_arena, s_pool, total_size);

  SE_RET_IF_ERR(vm_obj_table_build(&g_vm_obj_table, &s_arena, obj_cnt));
  SE_RET_IF_ERR(vm_accessor_table_build(&g_vm_accessor_table, &s_arena, acc_cnt));
  s_state = VM_LOAD_OPEN;
  return NULL;
}

err_h vm_loader_add_obj(uint16_t id, uint16_t item_count, uint8_t type, uint8_t flags, const char* name,
                        uint8_t name_len) {
  SE_RET_IF_ERR(require_state(VM_LOAD_OPEN));

  // vm_obj_create() takes a NUL-terminated name; the wire form is not
  if (name_len > VM_OBJ_NAME_MAX) {
    SE_RET_ERR(ERR_VM_OBJ_NAME_TOO_LONG, .len = name_len);
  }
  char name_buf[VM_OBJ_NAME_MAX + 1] = {0};
  if (name_len && name) memcpy(name_buf, name, name_len);

  vm_obj_h obj = NULL;
  SE_RET_IF_ERR(vm_obj_create(&obj, &s_arena,
                              &(vm_obj_cfg_t){
                                  .type = (vm_obj_t_e)type,
                                  .item_count = item_count,
                                  .name = name_len ? name_buf : NULL,
                                  .mutable = (flags & VM_LOAD_F_MUTABLE) != 0,
                                  .usr_mutable = (flags & VM_LOAD_F_USR_MUTABLE) != 0,
                                  .upd_resetable = (flags & VM_LOAD_F_UPD_RESETABLE) != 0,
                                  .retentive = (flags & VM_LOAD_F_RETENTIVE) != 0,
                              }));

  SE_RET_IF_ERR(vm_obj_table_set(&g_vm_obj_table, id, obj));
  return NULL;
}

err_h vm_loader_set_data(uint16_t id, uint16_t start_idx, const uint8_t* data, uint16_t len) {
  SE_RET_IF_ERR(require_state(VM_LOAD_OPEN));
  SE_CHECK_NOT_NULL(data);

  vm_obj_h obj = vm_obj_by_id(id);
  if (!obj) {
    SE_RET_ERR(ERR_VM_ACCESSOR_UNKNOWN_ID, .id = id);
  }

  uint16_t items = vm_obj_items_cnt(obj);

  if ((vm_obj_t_e)obj->head.d.obj_t == VM_OBJ_PTR) {
    /* Children arrive as ids, never as addresses -- a pointer is meaningless
       outside this boot's arena. Two bytes each, little-endian. */
    if ((len & 1u) != 0) {
      SE_RET_ERR(ERR_VM_LOAD_DATA_RANGE, .id = id, .start_idx = start_idx, .len = len, .items = items);
    }
    uint16_t n = len / 2u;
    if ((uint32_t)start_idx + n > items) {
      SE_RET_ERR(ERR_VM_LOAD_DATA_RANGE, .id = id, .start_idx = start_idx, .len = n, .items = items);
    }
    for (uint16_t i = 0; i < n; i++) {
      uint16_t child_id = (uint16_t)(data[i * 2] | ((uint16_t)data[i * 2 + 1] << 8));
      vm_obj_h child = vm_obj_by_id(child_id);
      if (!child) {
        // forward reference, or an id the program never created
        SE_RET_ERR(ERR_VM_ACCESSOR_UNKNOWN_ID, .id = child_id);
      }
      SE_RET_IF_ERR(vm_obj_link_direct(obj, (uint16_t)(start_idx + i), child));
    }
    return NULL;
  }

  uint8_t w = vm_obj_type_size(obj);
  if (w == 0 || (len % w) != 0) {
    SE_RET_ERR(ERR_VM_LOAD_DATA_RANGE, .id = id, .start_idx = start_idx, .len = len, .items = items);
  }
  uint16_t n = (uint16_t)(len / w);
  if ((uint32_t)start_idx + n > items) {
    SE_RET_ERR(ERR_VM_LOAD_DATA_RANGE, .id = id, .start_idx = start_idx, .len = n, .items = items);
  }

  memcpy(obj->payload + (size_t)start_idx * w, data, len);
  return NULL;
}

err_h vm_loader_add_accessor(uint16_t acc_id, uint16_t root_obj_id, uint8_t idx_count, const uint8_t* idx_data,
                             uint16_t idx_len) {
  SE_RET_IF_ERR(require_state(VM_LOAD_OPEN));

  vm_accessor_t* acc = NULL;
  SE_RET_IF_ERR(vm_accessor_create(&acc, &s_arena, root_obj_id, idx_count));

  size_t off = 0;
  for (uint8_t i = 0; i < idx_count; i++) {
    if (off + 1 > idx_len) {
      SE_RET_ERR(ERR_VM_LOAD_SHORT_RECORD, .packet = 0x44, .need = 1, .got = (uint16_t)(idx_len - off));
    }
    uint8_t kind = idx_data[off++];

    switch (kind) {
      case VM_IDX_LITERAL: {
        if (off + 4 > idx_len) {
          SE_RET_ERR(ERR_VM_LOAD_SHORT_RECORD, .packet = 0x44, .need = 4, .got = (uint16_t)(idx_len - off));
        }
        uint32_t v = (uint32_t)idx_data[off] | ((uint32_t)idx_data[off + 1] << 8) |
                     ((uint32_t)idx_data[off + 2] << 16) | ((uint32_t)idx_data[off + 3] << 24);
        off += 4;
        SE_RET_IF_ERR(vm_accessor_set_literal(acc, i, v));
        break;
      }
      case VM_IDX_REF: {
        if (off + 2 > idx_len) {
          SE_RET_ERR(ERR_VM_LOAD_SHORT_RECORD, .packet = 0x44, .need = 2, .got = (uint16_t)(idx_len - off));
        }
        uint16_t ref_id = (uint16_t)(idx_data[off] | ((uint16_t)idx_data[off + 1] << 8));
        off += 2;
        /* Must already be built. Requiring that is what makes a reference
           cycle unconstructable rather than merely depth-capped later. */
        vm_accessor_t* ref = vm_accessor_by_id(ref_id);
        if (!ref) {
          SE_RET_ERR(ERR_VM_ACC_TABLE_OOB, .id = ref_id, .count = g_vm_accessor_table.count);
        }
        SE_RET_IF_ERR(vm_accessor_set_ref(acc, i, ref));
        break;
      }
      case VM_IDX_NAME: {
        if (off + 1 > idx_len) {
          SE_RET_ERR(ERR_VM_LOAD_SHORT_RECORD, .packet = 0x44, .need = 1, .got = (uint16_t)(idx_len - off));
        }
        uint8_t nlen = idx_data[off++];
        if (off + nlen > idx_len) {
          SE_RET_ERR(ERR_VM_LOAD_SHORT_RECORD, .packet = 0x44, .need = nlen, .got = (uint16_t)(idx_len - off));
        }
        SE_RET_IF_ERR(vm_accessor_set_name(acc, i, &s_arena, (const char*)(idx_data + off), nlen));
        off += nlen;
        break;
      }
      default:
        SE_RET_ERR(ERR_VM_ACC_BAD_KIND, .acc_id = acc_id, .pos = i, .kind = kind);
    }
  }

  SE_RET_IF_ERR(vm_accessor_table_set(&g_vm_accessor_table, acc_id, acc));
  return NULL;
}

vm_load_state_e vm_loader_state(void) {
  return s_state;
}

uint32_t vm_loader_used(void) {
  return vm_alloc_used(&s_arena);
}
