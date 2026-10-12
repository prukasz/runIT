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

typedef struct __attribute__((aligned(8))) {
  uint32_t period;  //@description Time between pulses, in the chosen time unit @min 1
  uint8_t time_base;  //@alias Time unit @description Unit of the period @enum-ref vm_timer_unit_e
  uint8_t flags;  //@description VM_PERIODIC_F_* @runtime
  uint16_t _pad;
  uint64_t next_ms;  //@description Next deadline @runtime
} vm_block_periodic_data_t;

_Static_assert(sizeof(vm_block_periodic_data_t) == 16, "vm_block_periodic_data_t must be 16 bytes");
_Static_assert(offsetof(vm_block_periodic_data_t, time_base) == 4, "time_base must be at offset 4");
_Static_assert(offsetof(vm_block_periodic_data_t, next_ms) == 8, "next_ms must be at offset 8");
#define VM_PERIODIC_CUSTOM_LEN sizeof(vm_block_periodic_data_t)

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's tags: @alias (the name a user sees), @value, ... (vm-block-annotations.md).
   An unwired pin uses the state field it @overrides. */
typedef enum vm_in_periodic_e {
  VM_IN_PERIODIC_PERIOD = 0,  //@alias Period @description Time between pulses, in the chosen time unit; 0 pauses. @value u32 @overrides period
} vm_in_periodic_e;

//#block VM_BLK_PERIODIC
//@alias Every
//@category time
//@description Pulses ENO (the tick) for one pass every period while enabled; never bursts after a slow pass.
//@activation enabled
//@when Runs every pass while enabled.
//@face Every | {period} {time_base}
//@loop refuse
//@eno-alias Tick
//@eno-description One-pass pulse every period: put it on another block's Run when to run that block every period.
//@data vm_block_periodic_data_t
//@rule time_base is a vm_timer_unit_e value. @error ERR_VM_BLK_BAD_SHAPE
//@rule With the period input unwired, period is not 0. @error ERR_VM_BLK_BAD_SHAPE
/* Load-time check of the block's state; the pin shape comes from the pin enums above. */
bool vm_verify_periodic(vm_block_h b);
/* The body, called every pass. */
void vm_blk_periodic(vm_block_h b);
