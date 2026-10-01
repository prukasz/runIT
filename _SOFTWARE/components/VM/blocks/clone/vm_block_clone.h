#pragma once
#include "vm_block_helpers.h"

#define VM_CLONE_CUSTOM_LEN 0u

#define VM_CLONE_IN_SRC 0u  // source -- the pin whose freshness fires the block
#define VM_CLONE_IN_CELL 1u // target -- a pointer cell this block re-points

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

/* The body, called every pass. */
void vm_blk_clone(vm_block_h b);

//#vm-block VM_BLK_CLONE @id 7
//@title Clone
//@category data
//@activation triggered Runs when the source (input 0) is fresh, or on each pass an enable fires (an open gate, a tick), and the block is enabled.
//@block-description Copies the source into a tree this block owns, hung off a pointer cell; rebuilds only when the source's shape changes.
//@in 0 source @title Source @value object @macro VM_CLONE_IN_SRC @required
//@in 1 cell @title Pointer cell @value ptr-cell @macro VM_CLONE_IN_CELL @required
