#pragma once
#include "vm_block_helpers.h"

#define VM_CLONE_CUSTOM_LEN 0u

/*
 *           -------------
 *  ->EN     |           | ->ENO
 *  ->SOURCE |   CLONE   |
 *  ->CELL   |           |
 *           -------------
 *
 *  VM_BLK_CLONE -- Dynamic tree clone (IN0: src -> IN1: pointer cell). Update-driven, stateless.
 *  Like SET, but builds or reshapes the destination heap tree (vm_obj_dyn) dynamically to match
 *  source schema, then refills values without allocation on subsequent passes.
 */

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's tags: @alias (the name a user sees), @value, ... (vm-block-annotations.md).
   An unwired pin uses the state field it @overrides. */
typedef enum vm_in_clone_e {
  VM_IN_CLONE_SOURCE = 0,  //@alias Copy from @value object @required
  VM_IN_CLONE_CELL,  //@alias Copy to @value ptr-cell @required @user-write
} vm_in_clone_e;

//#block VM_BLK_CLONE
//@alias Clone
//@category data
//@description Copies the source into a tree this block owns, hung off a pointer cell; rebuilds only when the source's shape changes.
//@activation triggered
//@when Runs when the source (the first input) is fresh, or on each pass an enable fires (an open gate, a tick), and the block is enabled.
/* The body, called every pass. */
void vm_blk_clone(vm_block_h b);
