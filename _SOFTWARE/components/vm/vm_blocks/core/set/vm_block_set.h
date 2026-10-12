#pragma once
#include "vm_block_helpers.h"

/*
 *           -------------
 *  ->EN     |           | ->ENO
 * (->SOURCE)|    SET    |
 *  ->DST    |           |
 *           -------------
 *
 *  VM_BLK_SET -- Deep payload copy (IN0: src -> IN1: dst). Update-driven.
 *  Copies whole trees into an existing matching shape without allocation. Types and
 *  element counts must match exactly (no conversion). Both pins are inputs; ENO
 *  signals a write completed this pass.
 *
 *  Constant mode: with SOURCE unwired the block writes its `value` constant to the first
 *  scalar of DST (converted to DST's type: rounded, saturated) instead of copying. It runs
 *  when an enable fires (a tick, an open gate), or once on the first pass when no enable is
 *  wired. A trigger over DST would re-fire forever, so DST never triggers it.
 */

#define VM_SET_F_DONE (1u << 0) // Constant written at least once (the no-enable case writes once)

typedef struct __attribute__((aligned(4))) {
  float   value;  //@description Constant written to the destination while the Source input is unwired
  uint8_t flags;  //@description VM_SET_F_* bits @runtime
  uint8_t _pad[3];  //@description Align to 8 bytes
} vm_block_set_data_t;

_Static_assert(sizeof(vm_block_set_data_t) == 8, "vm_block_set_data_t must be 8 bytes");
#define VM_SET_CUSTOM_LEN sizeof(vm_block_set_data_t)

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's tags: @alias (the name a user sees), @value, ... (vm-block-annotations.md).
   An unwired pin uses the state field it @overrides. */
typedef enum vm_in_set_e {
  VM_IN_SET_SOURCE = 0,  //@alias Source @description The object to copy. Leave it unwired to write the Value constant instead. @value object @overrides value
  VM_IN_SET_DESTINATION,  //@alias Destination @description The object to write. @value object @required @user-write
} vm_in_set_e;

//#block VM_BLK_SET
//@alias Set
//@category data
//@description Copies the source payload (whole trees included) into the destination when the source is fresh, or writes a constant when the source is unwired. Types and counts must match.
//@activation triggered
//@when Runs when the source (the first input) is fresh, or on each pass an enable fires (an open gate, a tick), and the block is enabled. With no source it writes its constant when an enable fires (once at start without enables).
//@data vm_block_set_data_t
//@rule value is a finite number. @error ERR_VM_BLK_BAD_SHAPE
/* Load-time check of the block's state; the pin shape comes from the pin enum above. */
bool vm_verify_set(vm_block_h b);
/* The body, called every pass. */
void vm_blk_set(vm_block_h b);
