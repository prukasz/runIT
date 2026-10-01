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

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's //@in / //@out says what the pin carries and
   which state field it replaces while unwired; titles and descriptions are in set.display.json. */
//#block-enum @alias Set Inputs
typedef enum vm_in_set_e {
  VM_IN_SET_SOURCE = 0,    //@in @value object @required
  VM_IN_SET_DESTINATION,   //@in @value object @required
} vm_in_set_e;

/* The body, called every pass.
   Face, titles and descriptions: set.display.json; the app's set.content.json is generated from this header. */
//#vm-block VM_BLK_SET @id 6
//@activation triggered
void vm_blk_set(vm_block_h b);
