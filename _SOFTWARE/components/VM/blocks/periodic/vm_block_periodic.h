#pragma once
#include "vm_block_helpers.h"
#include "vm_block_timer.h"  // vm_timer_unit_e and its ms scale
#include "vm_exec.h"

/*
 *            -------------
 *  ->EN      |           | ->ENO
 *  ->PERIOD  | PERIODIC  |
 *            -------------
 *
 *  VM_BLK_PERIODIC -- "Every N": a one-pass pulse on ENO (the tick) every PERIOD
 *  while enabled. The root of a timed chain.
 *
 *  - The first tick is on the pass the block becomes enabled, then every PERIOD.
 *    Disabling stops it; enabling again restarts the phase.
 *  - The deadline advances by exactly PERIOD, so the rate doesn't drift. A pass
 *    that arrives after the deadline ticks at once (late, not skipped).
 *  - Never more than one tick per pass: ticks a slow pass missed entirely are
 *    dropped, not burst. Dropping is reported once per overrun episode
 *    (ERR_VM_PERIODIC_OVERRUN) and re-armed by the next on-time tick. The pass
 *    (10 ms floor) is the resolution: a shorter PERIOD overruns every pass.
 *  - Time is program time (vm_now_ms): a pause, a stop or stepping doesn't count, so
 *    resuming never reports the ticks that "passed" meanwhile as dropped.
 *  - PERIOD comes from the pin, or from custom_data when the pin is unwired, in
 *    `time_base` units. A PERIOD of 0 from the pin pauses ticking.
 */

#define VM_PERIODIC_F_ARMED (1u << 0)    // Running: next_ms is valid
#define VM_PERIODIC_F_OVERRUN (1u << 1)  // Current overrun episode already reported

//@data vm_block_periodic_data_t
typedef struct __attribute__((aligned(8))) {
  uint32_t period;    // @description In time_base units
  uint8_t time_base;  // @description Unit of period @enum-ref vm_timer_unit_e
  uint8_t flags;      // @description VM_PERIODIC_F_* @runtime
  uint16_t _pad;
  uint64_t next_ms;   // @description Next deadline @runtime
} vm_block_periodic_data_t;

_Static_assert(sizeof(vm_block_periodic_data_t) == 16, "vm_block_periodic_data_t must be 16 bytes");
_Static_assert(offsetof(vm_block_periodic_data_t, time_base) == 4, "time_base must be at offset 4");
_Static_assert(offsetof(vm_block_periodic_data_t, next_ms) == 8, "next_ms must be at offset 8");
#define VM_PERIODIC_CUSTOM_LEN sizeof(vm_block_periodic_data_t)

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's //@in / //@out says what the pin carries and
   which state field it replaces while unwired; titles and descriptions are in periodic.display.json. */
//#block-enum @alias Every Inputs
typedef enum vm_in_periodic_e {
  VM_IN_PERIODIC_PERIOD = 0,   //@in @value u32 @overrides period
} vm_in_periodic_e;

/* Load-time check of the block's state; the pin shape comes from the pin enums above. */
//@rule time_base is a vm_timer_unit_e value. @error ERR_VM_BLK_BAD_SHAPE
//@rule With the period input unwired, period is not 0. @error ERR_VM_BLK_BAD_SHAPE
bool vm_verify_periodic(vm_block_h b);
/* The body, called every pass.
   Face, titles and descriptions: periodic.display.json; the app's periodic.content.json is generated from this header. */
//#vm-block VM_BLK_PERIODIC @id 13
//@activation enabled
void vm_blk_periodic(vm_block_h b);
