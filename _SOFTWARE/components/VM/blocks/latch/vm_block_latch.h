#pragma once
#include "vm_block_helpers.h"

/*
 *           -------------
 *  ->EN     |           | ->ENO
 *  ->RESET  |   LATCH   |
 *           -------------
 *
 *  VM_BLK_LATCH -- Bistable (IEC 61131-3 SR / RS). Turns a one-pass pulse (EDGE,
 *  PERIODIC, an event) into a held level, so anything time-based below it can
 *  accumulate: event -> latch -> wait -> act.
 *
 *    set dominant   (SR): ENO = EN or (ENO and not RESET)
 *    reset dominant (RS): ENO = not RESET and (EN or ENO)
 *
 *  EN sets the held state; RESET clears it even when EN is false. RESET may be
 *  unwired (reads false), but EN must have at least one source. ENO reflects the
 *  held state, so it remains true after a one-pass EN pulse until RESET wins.
 */

//#block-enum @alias Latch Mode
typedef enum {
  VM_LATCH_SET_DOMINANT = 0,    // SR: SET wins when both are true
  VM_LATCH_RESET_DOMINANT = 1,  // RS: RESET wins when both are true
  VM_LATCH_MODE_CNT = 2,
} vm_latch_mode_e;

#define VM_LATCH_F_HELD (1u << 0)  // Current ENO state

//@data vm_block_latch_data_t
typedef struct __attribute__((aligned(4))) {
  uint8_t mode;   // @description Which signal wins when EN and RESET are true @enum-ref vm_latch_mode_e
  uint8_t flags;  // @description VM_LATCH_F_* @runtime
  uint16_t _pad;
} vm_block_latch_data_t;

_Static_assert(sizeof(vm_block_latch_data_t) == 4, "vm_block_latch_data_t must be 4 bytes");
#define VM_LATCH_CUSTOM_LEN sizeof(vm_block_latch_data_t)

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's //@in / //@out says what the pin carries and
   which state field it replaces while unwired; titles and descriptions are in latch.display.json. */
//#block-enum @alias Latch Inputs
typedef enum vm_in_latch_e {
  VM_IN_LATCH_RESET = 0,   //@in @value bool
} vm_in_latch_e;

/* Load-time check of the block's state; the pin shape comes from the pin enums above. */
//@rule At least one EN source is connected; no data outputs. @error ERR_VM_BLK_BAD_SHAPE
//@rule mode is a vm_latch_mode_e value. @error ERR_VM_BLK_BAD_SHAPE
bool vm_verify_latch(vm_block_h b);
/* The body, called every pass.
   Face, titles and descriptions: latch.display.json; the app's latch.content.json is generated from this header. */
//#vm-block VM_BLK_LATCH @id 12
//@activation enabled
//@enables required
void vm_blk_latch(vm_block_h b);
