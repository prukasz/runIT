#pragma once
#include "vm_block_helpers.h"
#include "vm_exec.h"

/*
 *           -------------
 *  ->EN     |   TIMER   | ->ENO
 *  ->IN     | (TON/TOF/ |
 *  ->PT     |    TP)    | ->ET
 *           -------------
 *
 *  VM_BLK_TIMER -- IEC standard timer (TON, TOF, TP, plus inverted variants).
 *  Computes the output Q (published as ENO) and elapsed time ET from input trigger and preset duration PT.
 *  Time base is load-configurable: ms (0), s (1), min (2), hr (3).
 */

//#ref-enum @alias Timer Mode
typedef enum {
  VM_TIMER_TON      = 0,  // On-Delay
  VM_TIMER_TOF      = 1,  // Off-Delay
  VM_TIMER_TP       = 2,  // Pulse Timer
  VM_TIMER_TON_INV  = 3,  // Inverted On-Delay (!Q)
  VM_TIMER_TOF_INV  = 4,  // Inverted Off-Delay (!Q)
  VM_TIMER_TP_INV   = 5,  // Inverted Pulse Timer (!Q)
  VM_TIMER_MODE_CNT = 6,
} vm_timer_mode_e;

//#ref-enum @alias Time Unit
typedef enum {
  VM_TIMER_UNIT_MS  = 0,  // Milliseconds (1 ms)
  VM_TIMER_UNIT_SEC = 1,  // Seconds (1,000 ms)
  VM_TIMER_UNIT_MIN = 2,  // Minutes (60,000 ms)
  VM_TIMER_UNIT_HR  = 3,  // Hours (3,600,000 ms)
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
  uint8_t  mode;        // vm_timer_mode_e @enum-ref vm_timer_mode_e
  uint8_t  flags;       // VM_TIMER_F_*: only VM_TIMER_F_INVERTED (0x08) is set by the app
  uint8_t  time_base;   // Unit of pt and ET @enum-ref vm_timer_unit_e
  uint8_t  _pad1;
  uint32_t _pad2;
  uint32_t pt;          // Preset time in configured unit (hardcoded fallback)
  uint64_t start_ms;    // Timestamp when timing started @runtime
  uint32_t elapsed;     // Current elapsed time @runtime
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

#define VM_TIMER_IN_SIGNAL 0u
#define VM_TIMER_IN_PT 1u
#define VM_TIMER_ET 0u  // the only output pin: Q is the block's ENO

/* Load-time check of the block's state; the pin shape is checked from the //@in / //@out directives below. */
bool vm_verify_timer(vm_block_h b);
/* The body, called every pass. */
void vm_blk_timer(vm_block_h b);

//#vm-block VM_BLK_TIMER @id 9
//@title Timer
//@category time
//@activation enabled Runs every pass while enabled.
//@data vm_block_timer_data_t
//@block-description On-delay, off-delay or pulse timer (plus inverted). ENO is the timer's output Q, held as a level; Elapsed counts in time_base units.
//@header Timer | {mode} {pt} {time_base}
//@eno @title Q @description The timer's output level: true after the on-delay, during a pulse, or until the off-delay ends. Put it on another block's Run when to gate that block.
//@rule mode is a vm_timer_mode_e value and time_base a vm_timer_unit_e value. @error ERR_VM_BLK_BAD_SHAPE
//@in 0 in @title Start @description The signal the timer acts on; the block's enable gates the whole timer. @value bool @macro VM_TIMER_IN_SIGNAL @required
//@in 1 pt @title Preset @description Overrides pt, in time_base units. @value u32 @overrides pt @macro VM_TIMER_IN_PT
//@out 0 et @title Elapsed time @description Time elapsed, in time_base units. @value u32 @macro VM_TIMER_ET
