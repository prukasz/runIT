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

typedef enum vm_in_clone_e {
  VM_IN_CLONE_SOURCE = 0,
  VM_IN_CLONE_CELL,
} vm_in_clone_e;

/* The body, called every pass. */
void vm_blk_clone(vm_block_h b);
