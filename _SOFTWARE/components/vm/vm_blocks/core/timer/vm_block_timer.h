#pragma once
#include "vm_block_helpers.h"
#include "vm_exec.h"

/*
 *           -------------
 *  ->EN     |   TIMER   | ->ENO
 *  ->PT     | (TON/TOF/ | ->ET
 *           |    TP)    |
 *           -------------
 *
 *  VM_BLK_TIMER -- IEC standard timer (TON, TOF, TP, plus inverted variants).
 *  Computes the output Q (published as ENO) and elapsed time ET from the enable EN (the timer's input) and preset duration PT.
 *  Time base is load-configurable: ms (0), s (1), min (2), hr (3).
 */

//#block-enum @alias Timer Mode
typedef enum {
  VM_TIMER_TON      = 0,  //@alias On-delay @description The output turns on after the preset time
  VM_TIMER_TOF      = 1,  //@alias Off-delay @description The output turns off after the preset time
  VM_TIMER_TP       = 2,  //@alias Pulse @description The output is on for the preset time
  VM_TIMER_TON_INV  = 3,  //@alias Inverted on-delay @description On-delay with the output inverted
  VM_TIMER_TOF_INV  = 4,  //@alias Inverted off-delay @description Off-delay with the output inverted
  VM_TIMER_TP_INV   = 5,  //@alias Inverted pulse @description Pulse with the output inverted
  VM_TIMER_MODE_CNT = 6,
} vm_timer_mode_e;

//#ref-enum @alias Time Unit
typedef enum {
  VM_TIMER_UNIT_MS  = 0,  //@alias Milliseconds @description 1 ms
  VM_TIMER_UNIT_SEC = 1,  //@alias Seconds @description 1,000 ms
  VM_TIMER_UNIT_MIN = 2,  //@alias Minutes @description 60,000 ms
  VM_TIMER_UNIT_HR  = 3,  //@alias Hours @description 3,600,000 ms
  VM_TIMER_UNIT_CNT = 4,
} vm_timer_unit_e;

static inline uint64_t vm_timer_unit_scale_ms(uint8_t unit) {
  switch (unit) {
    case VM_TIMER_UNIT_SEC: return 1000ULL;
    case VM_TIMER_UNIT_MIN: return 60000ULL;
    case VM_TIMER_UNIT_HR:  return 3600000ULL;
    case VM_TIMER_UNIT_MS:
    default:                return 1ULL;
  }
}

#define VM_TIMER_F_INITIALIZED (1u << 0)
#define VM_TIMER_F_RUNNING     (1u << 1)
#define VM_TIMER_F_PREV_IN     (1u << 2)
#define VM_TIMER_F_INVERTED    (1u << 3)

typedef struct __attribute__((aligned(8))) {
  uint8_t  mode;  //@alias Mode @description How the timer behaves: on-delay, off-delay or pulse, and their inverted versions @enum-ref vm_timer_mode_e
  uint8_t  flags;  //@alias Options @description Option bits: only the inverted-output bit (VM_TIMER_F_INVERTED, 0x08) is set by the app
  uint8_t  time_base;  //@alias Time unit @description Unit of the preset and of the elapsed time @enum-ref vm_timer_unit_e
  uint8_t  _pad1;
  uint32_t _pad2;
  uint32_t pt;  //@description The preset time, in the chosen time unit (used while the Preset input is unwired)
  uint64_t start_ms;  //@description Timestamp when timing started @runtime
  uint32_t elapsed;  //@description Current elapsed time @runtime
} vm_block_timer_data_t;

_Static_assert(sizeof(vm_block_timer_data_t) == 32, "vm_block_timer_data_t must be 32 bytes");
_Static_assert(offsetof(vm_block_timer_data_t, time_base) == 2, "time_base must be at offset 2");
_Static_assert(offsetof(vm_block_timer_data_t, pt) == 8, "pt must be at offset 8");
_Static_assert(offsetof(vm_block_timer_data_t, start_ms) == 16, "start_ms must be at offset 16");
_Static_assert(offsetof(vm_block_timer_data_t, elapsed) == 24, "elapsed must be at offset 24");

/**
 * @brief Initialize timer configuration in block custom data.
 */
static inline void vm_block_timer_init_data(void* buffer, vm_timer_mode_e mode, uint32_t pt, bool inverted) {
  const vm_block_timer_data_t data = {
      .mode = (uint8_t)mode,
      .time_base = (uint8_t)VM_TIMER_UNIT_MS,
      .pt = pt,
      .flags = inverted ? VM_TIMER_F_INVERTED : 0,
  };
  memcpy(buffer, &data, sizeof(data));
}

static inline void vm_block_timer_init_data_ex(void* buffer, vm_timer_mode_e mode, vm_timer_unit_e unit, uint32_t pt, bool inverted) {
  const vm_block_timer_data_t data = {
      .mode = (uint8_t)mode,
      .time_base = (uint8_t)unit,
      .pt = pt,
      .flags = inverted ? VM_TIMER_F_INVERTED : 0,
  };
  memcpy(buffer, &data, sizeof(data));
}

/* Pins, by index: VM_IN_<BLOCK>_<PIN> / VM_OUT_<BLOCK>_<PIN>. Each member's tags: @alias (the name a user sees), @value, ... (vm-block-annotations.md).
   An unwired pin uses the state field it @overrides. */
typedef enum vm_in_timer_e {
  VM_IN_TIMER_PT = 0,  //@alias Preset @description The preset time, in the chosen time unit. @value u32 @overrides pt
} vm_in_timer_e;

typedef enum vm_out_timer_e {
  VM_OUT_TIMER_ET = 0,  //@alias Elapsed time @description Time elapsed, in the chosen time unit. @value u32
} vm_out_timer_e;

//#block VM_BLK_TIMER
//@alias Timer
//@category time
//@description On-delay, off-delay or pulse timer (plus inverted), driven by the block's enable. ENO is the timer's output Q, held as a level; Elapsed counts in the chosen time unit.
//@activation enabled
//@when Runs every pass while enabled.
//@face Timer | {mode} {pt} {time_base}
//@loop refuse
//@eno-alias Q
//@eno-description The timer's output level: true after the on-delay, during a pulse, or until the off-delay ends. Put it on another block's Run when to gate that block.
//@data vm_block_timer_data_t
//@rule mode is a vm_timer_mode_e value and time_base a vm_timer_unit_e value. @error ERR_VM_BLK_BAD_SHAPE
/* Load-time check of the block's state; the pin shape comes from the pin enums above. */
bool vm_verify_timer(vm_block_h b);
/* The body, called every pass. */
void vm_blk_timer(vm_block_h b);
