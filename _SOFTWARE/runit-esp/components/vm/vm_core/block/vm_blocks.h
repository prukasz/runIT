#pragma once

/*
The palette: every block type this firmware can run, as a table of pointers indexed by block type (vm_block_ids.h).
A block type declares itself with VM_BLOCK_TYPE_DEFINE in its own file and registers before app_main, the way a device
driver does (SYS_DEVICE_TYPE_REGISTER); nothing central lists the blocks. The component that holds the block files is
linked WHOLE_ARCHIVE, because nothing references a block and the linker would drop the registration.
*/

#include "vm_block_helpers.h"
#include "vm_block_ids.h"

/* ========================================================================= */
/* Registry & lookups                                                        */
/* ========================================================================= */

/* One pointer per block type, indexed by block_type; NULL = not in the palette. Written only before app_main. */
extern const vm_block_type_t* g_vm_block_types[VM_BLOCK_TYPES_MAX];

/** @brief Adds a type under @p id. A bad or taken id is a programming error: it aborts at boot. */
void vm_block_type_register(uint8_t id, const vm_block_type_t* type);

/** @brief How many block types are registered. */
uint16_t vm_block_types_count(void);

/**
 * @brief Defines a block type and registers it before app_main. `ID` is the VM_BLK_ name: it names the statics, so a
 * file may define several types.
 * @usage VM_BLOCK_TYPE_DEFINE(VM_BLK_SET, .run = vm_blk_set, .check = vm_verify_set, .min_in = 2, .max_in = 2,
 *                             .required_in = 0x2u, .state_len = sizeof(vm_block_set_data_t));
 */
#define VM_BLOCK_TYPE_DEFINE(ID, ...)                                            \
  static const vm_block_type_t s_vm_block_type_##ID = {__VA_ARGS__};             \
  __attribute__((constructor)) static void s_vm_block_type_register_##ID(void) { \
    vm_block_type_register((ID), &s_vm_block_type_##ID);                         \
  }

/** @brief Palette entry for @p block_type; NULL if no block type has that id. */
static inline const vm_block_type_t* vm_block_type_of(uint8_t block_type) {
  return block_type < VM_BLOCK_TYPES_MAX ? g_vm_block_types[block_type] : NULL;
}

/** @brief Body of @p block_type; NULL if not in the palette. */
static inline vm_block_fn vm_block_fn_for(uint8_t block_type) {
  const vm_block_type_t* type = vm_block_type_of(block_type);
  return type ? type->run : NULL;
}

/**
 * @brief Load-time check of a built block against its palette entry: pin
 * counts, required pins, private-state size, then the type's own check.
 * Run once by vm_block_create(); a block that fails never runs.
 */
static inline bool vm_block_verify(vm_block_h b) {
  const vm_block_type_t* type = vm_block_type_of(b->cfg.block_type);
  if (!type) return false;
  if (!vm_block_shape_valid(b, type)) return false;
  if (b->cfg.custom_len < type->state_len) return false;
  return type->check ? type->check(b) : true;
}
