#pragma once
#include "vm_block_helpers.h"

/*
 *           -------------
 *  ->EN     |    IF     | ->ENO
 *  ->COND   |           | ->TRUE
 *           |           | ->FALSE
 *           -------------
 *
 *  VM_BLK_IF -- two-way flow router. Enable-driven and stateless (custom_len = 0).
 */

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's //@in / //@out says what the pin carries and
   which state field it replaces while unwired; titles and descriptions are in if.display.json. */
//#block-enum @alias If Inputs
typedef enum vm_in_if_e {
  VM_IN_IF_CONDITION = 0,   //@in @value bool @required
} vm_in_if_e;

//#block-enum @alias If Outputs
typedef enum vm_out_if_e {
  VM_OUT_IF_YES = 0,   //@out @value gate @required
  VM_OUT_IF_NO,        //@out @value gate @required
} vm_out_if_e;

/* The body, called every pass.
   Face, titles and descriptions: if.display.json; the app's if.content.json is generated from this header. */
//#vm-block VM_BLK_IF @id 3
//@activation enabled
void vm_blk_if(vm_block_h b);
