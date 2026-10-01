#pragma once

/*
The palette -- every block type this firmware can run.

This header is the id list and nothing else. A block type is a function plus a
wire format, and both of those live with the block: `vm_block_expr.h` carries
the RPN header, opcodes and evaluator, `vm_block_branch.h` the routers,
`vm_block_for.h` the loop. What has to be central is only the numbering,
because the id *is* the index into `g_vm_block_types[]` (blocks/vm_blocks_table.c)
-- and even that is not written here: each block states its own id (`@id`) in
its `//#vm-block` line, and `generate-vm-blocks.py` writes the VM_BLK_<NAME>
numbers to vm_block_ids.generated.h and the table entries to
vm_blocks_registry.generated.h.

Adding a block type is its own folder here (`blocks/<name>/vm_block_<name>.h` with
its `//#vm-block VM_BLK_<NAME> @id <n>` directives, `.c`, guide), then `idf.py
reconfigure` and `python data-structures/auto-annotations/generate-all.py`.
Nothing in this file or in vm_blocks_table.c changes.

Type 0 is deliberately unused. A `block_type` nobody set is zero, and it should
not resolve to a runnable block.
*/

#include "vm_block_ids.generated.h"
#include "vm_block_helpers.h"

/* ========================================================================= */
/* Palette table & lookups                                                   */
/* ========================================================================= */

/* One entry per block type, indexed by block_type (vm_blocks_table.c). */
extern const vm_block_type_t g_vm_block_types[];
extern const uint16_t g_vm_block_types_cnt;

/** @brief Palette entry for @p block_type; NULL if no block type has that id. */
static inline const vm_block_type_t* vm_block_type_of(uint8_t block_type) {
  if (block_type >= g_vm_block_types_cnt) return NULL;
  const vm_block_type_t* type = &g_vm_block_types[block_type];
  return type->run ? type : NULL;
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
