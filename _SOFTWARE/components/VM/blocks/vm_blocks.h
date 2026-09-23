#pragma once

/*
The palette -- every block type this firmware can run.

This header is the id list and nothing else. A block type is a function plus a
wire format, and both of those live with the block: `vm_block_expr.h` carries
the RPN header, opcodes and evaluator, `vm_block_branch.h` the routers,
`vm_block_for.h` the loop. What has to be central is only the numbering,
because the id *is* the index into `g_vm_block_types[]` (blocks/vm_blocks_table.c)
and a firmware has exactly one palette.

Adding a block type is its own header beside these, one `#define` here, and one
line in the table.

Type 0 is deliberately unused. A `block_type` nobody set is zero, and it should
not resolve to a runnable block.
*/

#define VM_BLK_NONE 0      // reserved: an unset block_type is not runnable
#define VM_BLK_EXPR 1      // RPN expression over floats      -- vm_block_expr.h
#define VM_BLK_EXPR_BIT 2  // RPN expression over uint32 bits -- vm_block_expr.h
#define VM_BLK_IF 3        // two-way flow router             -- vm_block_branch.h
#define VM_BLK_SWITCH 4    // n-way flow router               -- vm_block_branch.h
#define VM_BLK_FOR 5       // span owner: repeats the range after it -- vm_block_for.h
#define VM_BLK_SET 6       // copies a payload, source -> target      -- vm_block_set.h
#define VM_BLK_CLONE 7     // copies, building the destination first  -- vm_block_clone.h
#define VM_BLK_EDGE 8      // edge detector (rising, falling, both)   -- vm_block_edge.h
#define VM_BLK_TIMER 9     // timer (TON, TOF, TP + inverted)         -- vm_block_timer.h
#define VM_BLK_IO_SET_LEVEL 10 // hardware IO set level (digital out)   -- vm_block_io_set_level.h
#define VM_BLK_IO_TOGGLE 11    // hardware IO toggle (digital toggle)    -- vm_block_io_toggle.h
#define VM_BLK_LATCH 12        // bistable SR / RS: pulse -> held level  -- vm_block_latch.h
#define VM_BLK_PERIODIC 13     // "every N": one-pass tick per period    -- vm_block_periodic.h
#define VM_BLK_ACTION 14       // requests a system action (queued)      -- vm_block_action.h
#define VM_BLK_ON_EVENT 15     // starts a chain on a matching system event -- vm_block_on_event.h

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
  if (!vm_block_shape_valid(b, type->min_in, type->min_q, type->required_in)) return false;
  if (b->cfg.custom_len < type->state_len) return false;
  return type->check ? type->check(b) : true;
}
