#pragma once
#include <sdkconfig.h>
#include "sys_error.h"

#define VM_ID_NONE        0xFFFFu       // Allocate without binding to registry

typedef enum vm_reg_e {
  VM_REG_OBJ = 0,
  VM_REG_ACC = 1,
  VM_REG_BLK = 2,
  VM_REG_CNT = 3,
} vm_reg_e;

/** @brief String representation of registry kind for debugging. */
static inline const char* vm_reg_kind_name(vm_reg_e r) {
  return vm_reg_name((uint8_t)r);
}

typedef struct vm_alloc_t {
  uint8_t* base;
  uint32_t capacity;
  uint32_t offset;
} vm_alloc_t;

typedef struct vm_registry_t {
  void**   items;  // id -> pointer table
  uint16_t count;  // ids in range [0, count)
} vm_registry_t;

typedef struct vm_store_t {
  vm_alloc_t    arena;
  vm_registry_t reg[VM_REG_CNT];
} vm_store_t;

extern vm_store_t g_vm_store;

/** @brief Look up pointer by registry and ID; returns NULL if out of bounds. */
static inline void* vm_store_get(vm_reg_e r, uint16_t id) {
  const vm_registry_t* g = &g_vm_store.reg[r];
  return (id < g->count) ? g->items[id] : NULL;
}

/** @brief Free dynamic objects, detach registries, and release arena memory. */
void vm_store_reset(void);

/**
 * @brief Allocate arena pool and initialize object/accessor/block registries.
 * @param total_size Total arena bytes requested.
 * @param counts Item counts for each vm_reg_e registry.
 */
SE_MUST_USE err_h vm_store_open(uint32_t total_size, const uint16_t counts[VM_REG_CNT]);

/**
 * @brief Carve a zeroed, 4-aligned chunk from arena and bind to registry ID.
 * @param out Receives allocated pointer.
 * @param r Target registry (ignored if id == VM_ID_NONE).
 * @param id Registry ID or VM_ID_NONE.
 * @param size Allocation size in bytes.
 */
SE_MUST_USE err_h vm_store_alloc(void** out, vm_reg_e r, uint16_t id, uint32_t size);

/**
 * @brief Undo the most recent vm_store_alloc(): unbind @p id and rewind the arena.
 *
 * For a construction that allocates, then finds the result invalid, so a
 * rejected item costs neither its id nor arena space. Only valid while nothing
 * else was allocated since @p mark was taken.
 *
 * @code
 * uint32_t mark = vm_store_used();
 * SE_TRY(vm_store_alloc(&p, VM_REG_BLK, id, size));
 * if (!valid(p)) { vm_store_undo(VM_REG_BLK, id, mark); SE_FAIL(...); }
 * @endcode
 *
 * @param r Registry the id was bound in (ignored for VM_ID_NONE).
 * @param id Id passed to vm_store_alloc(), or VM_ID_NONE.
 * @param mark vm_store_used() taken just before that allocation.
 */
void vm_store_undo(vm_reg_e r, uint16_t id, uint32_t mark);

/** @brief Current bytes allocated from the arena. */
uint32_t vm_store_used(void);

/** @brief Total arena capacity in bytes. */
uint32_t vm_store_capacity(void);
