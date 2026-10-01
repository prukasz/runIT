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

//#ref-enum @alias Latch Mode
typedef enum {
  VM_LATCH_SET_DOMINANT = 0,    // SR: SET wins when both are true
  VM_LATCH_RESET_DOMINANT = 1,  // RS: RESET wins when both are true
  VM_LATCH_MODE_CNT = 2,
} vm_latch_mode_e;

#define VM_LATCH_F_HELD (1u << 0)  // Current ENO state

typedef struct __attribute__((aligned(4))) {
  uint8_t mode;   // Which signal wins when EN and RESET are true @enum-ref vm_latch_mode_e
  uint8_t flags;  // VM_LATCH_F_* @runtime
  uint16_t _pad;
} vm_block_latch_data_t;

_Static_assert(sizeof(vm_block_latch_data_t) == 4, "vm_block_latch_data_t must be 4 bytes");
#define VM_LATCH_CUSTOM_LEN sizeof(vm_block_latch_data_t)

#define VM_LATCH_IN_RESET 0u

/* Load-time check of the block's state; the pin shape is checked from the //@in / //@out directives below. */
bool vm_verify_latch(vm_block_h b);
/* The body, called every pass. */
void vm_blk_latch(vm_block_h b);

//#vm-block VM_BLK_LATCH @id 12
//@title Latch
//@category logic
//@activation enabled Samples EN and Reset every pass; EN sets the held state.
//@enables required
//@data vm_block_latch_data_t
//@block-description EN sets a held flow level; Reset clears it. ENO stays active until reset.
//@header Latch {mode}
//@rule At least one EN source is connected; no data outputs. @error ERR_VM_BLK_BAD_SHAPE
//@rule mode is a vm_latch_mode_e value. @error ERR_VM_BLK_BAD_SHAPE
//@in 0 reset @title Reset @value bool @macro VM_LATCH_IN_RESET
//@eno @title Held @description Held flow level: true after EN sets it, until Reset clears it.
