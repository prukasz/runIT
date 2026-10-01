#pragma once
#include "vm_block_helpers.h"

/*
 *           -------------
 *  ->EN     |           | ->ENO
 *  ->SOURCE |    SET    |
 *  ->DST    |           |
 *           -------------
 *
 *  VM_BLK_SET -- Deep payload copy (IN0: src -> IN1: dst). Update-driven, stateless.
 *  Copies whole trees into an existing matching shape without allocation. Types and
 *  element counts must match exactly (no conversion). Both pins are inputs; ENO
 *  signals a copy completed this pass.
 */

#define VM_SET_CUSTOM_LEN 0u

#define VM_SET_IN_SRC 0u  // source -- the pin whose freshness fires the block
#define VM_SET_IN_DST 1u  // target -- named, not read

/* The body, called every pass. */
void vm_blk_set(vm_block_h b);

//#vm-block VM_BLK_SET @id 6
//@title Set
//@category data
//@activation triggered Runs when the source (input 0) is fresh, or on each pass an enable fires (an open gate, a tick), and the block is enabled.
//@block-description Copies the source payload (whole trees included) into the destination when the source is fresh. Types and counts must match.
//@in 0 source @title Source @value object @macro VM_SET_IN_SRC @required
//@in 1 destination @title Destination @value object @macro VM_SET_IN_DST @required
