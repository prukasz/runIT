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

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's //@in / //@out says what the pin carries and
   which state field it replaces while unwired; titles and descriptions are in clone.display.json. */
//#block-enum @alias Clone Inputs
typedef enum vm_in_clone_e {
  VM_IN_CLONE_SOURCE = 0,   //@in @value object @required
  VM_IN_CLONE_CELL,         //@in @value ptr-cell @required
} vm_in_clone_e;

/* The body, called every pass.
   Face, titles and descriptions: clone.display.json; the app's clone.content.json is generated from this header. */
//#vm-block VM_BLK_CLONE @id 7
//@activation triggered
void vm_blk_clone(vm_block_h b);
