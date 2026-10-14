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

typedef enum {
  VM_LATCH_SET_DOMINANT = 0,  // When EN and RESET are both true the output is set
  VM_LATCH_RESET_DOMINANT = 1,  // When EN and RESET are both true the output is reset
  VM_LATCH_MODE_CNT = 2,
} vm_latch_mode_e;

#define VM_LATCH_F_HELD (1u << 0)  // Current ENO state

typedef struct __attribute__((aligned(4))) {
  uint8_t mode;  // Which signal wins when EN and RESET are true
  uint8_t flags;
  uint16_t _pad;
} vm_block_latch_data_t;

_Static_assert(sizeof(vm_block_latch_data_t) == 4, "vm_block_latch_data_t must be 4 bytes");
#define VM_LATCH_CUSTOM_LEN sizeof(vm_block_latch_data_t)

typedef enum vm_in_latch_e {
  VM_IN_LATCH_RESET = 0,
} vm_in_latch_e;

/* Load-time check of the block's state; the pin shape comes from the pin enums above. */
bool vm_verify_latch(vm_block_h b);
/* The body, called every pass. */
void vm_blk_latch(vm_block_h b);
