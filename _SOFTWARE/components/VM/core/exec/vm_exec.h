#pragma once
#include "esp_timer.h"
#include "sys_error.h"
#include "vm_block.h"


/* ========================================================================= */
/* Execution Modes & Control Commands                                        */
/* ========================================================================= */

typedef enum vm_run_mode_e {
  VM_RUN_STOPPED    = 0,  // Supervisor idle; explicit non-supervisor passes remain available
  VM_RUN_RUNNING    = 1,  // Continuous cyclic passes (1 pass per tick)
  VM_RUN_FROZEN     = 2,  // Paused before next block dispatch
  VM_RUN_STEP       = 3,  // Run single pass, then transition to FROZEN
  VM_RUN_SCAN       = 4,  // Scan mode; waits for VM_EXEC_ONCE
  VM_RUN_BLOCK      = 5,  // Block mode; waits for VM_EXEC_NEXT
  VM_RUN_BLOCK_STEP = 6,  // Execute single pending block dispatch
} vm_run_mode_e;

/** @brief String representation of VM run mode for debugging. */
static inline const char* vm_run_mode_str(vm_run_mode_e m) {
  return vm_run_mode_name((uint8_t)m);
}

typedef enum vm_exec_command_e {
  VM_EXEC_SCAN_MODE      = 0,
  VM_EXEC_ONCE           = 1,
  VM_EXEC_BLOCK_MODE     = 2,
  VM_EXEC_NEXT           = 3,
  VM_EXEC_RESET_TO_START = 4,
  VM_EXEC_NORMAL_MODE    = 5,
  VM_EXEC_PAUSE          = 6,
  VM_EXEC_RESUME         = 7,
  VM_EXEC_RESET          = 8,
  VM_EXEC_ACK_FAULT      = 9,
} vm_exec_command_e;

/** @brief String representation of VM execution command for debugging. */
static inline const char* vm_exec_command_str(vm_exec_command_e c) {
  return vm_exec_command_name((uint8_t)c);
}

typedef struct vm_exec_status_t {
  vm_run_mode_e mode;
  uint16_t      next_block;      // Target block ID if paused; UINT16_MAX otherwise
  bool          scan_active;     // Pass is currently executing
  bool          waiting;         // VM pass task parked before next_block
  bool          stop_requested;  // Cancellation requested; mode becomes STOPPED at quiescence
} vm_exec_status_t;

/** Stable snapshot of the first critical device fault since acknowledgment. */
typedef struct vm_exec_fault_status_t {
  bool      latched;
  uint8_t   device_id;
  err_tag_e root_tag;
  uint32_t  root_owner;
  uint32_t  occurrences;
} vm_exec_fault_status_t;

#include "vm_blocks.h"

/* ========================================================================= */
/* Palette Dispatch                                                          */
/* ========================================================================= */

/** @brief Validate block_type against the palette at load time. */
err_h vm_exec_check_block_type(uint16_t blk_id, uint8_t block_type);

/* ========================================================================= */
/* Clocks & Timestamps                                                       */
/* ========================================================================= */

extern uint64_t g_vm_pass_ms;  // Latched pass timestamp in ms

/** @brief Microseconds since boot, read live from timer. */
static inline uint64_t vm_clock_us(void) {
  return (uint64_t)esp_timer_get_time();
}

/** @brief Pass timestamp in ms, latched at top of current pass. Used by timing blocks. */
static inline uint64_t vm_now_ms(void) {
  return g_vm_pass_ms;
}

/* ========================================================================= */
/* Supervisor Task & Lifecycle                                               */
/* ========================================================================= */

/** @brief Start the supervisor FreeRTOS task on core 1. Idempotent. */
err_h vm_exec_start(void);

/**
 * @brief Request cancellation at the next block boundary without waiting.
 *
 * Safe from a block/sample hook and from other tasks. While a pass is active,
 * vm_exec_mode() retains its current value and status.stop_requested is true;
 * mode becomes VM_RUN_STOPPED only after the pass releases all VM handles.
 */
void vm_exec_request_stop(void);

/**
 * @brief Request stop and wait for a quiescent VM.
 *
 * On success, mode is VM_RUN_STOPPED and no pass is active or parked. Objects,
 * block state, and loaded program storage are preserved. If called by the task
 * currently executing a pass, cancellation is still requested but the wait is
 * rejected with ERR_VM_EXEC_SELF_BARRIER to avoid self-deadlock.
 */
err_h vm_exec_stop(void);

/**
 * @brief Latch a critical device fault and request cancellation immediately.
 * @return true only for the first fault in the current latched episode.
 */
bool vm_exec_fault_latch(uint8_t device_id, uint32_t root_owner, err_tag_e root_tag);

/** @brief Copy the persistent critical-fault snapshot. */
vm_exec_fault_status_t vm_exec_fault_status(void);

/** @brief Clear a latched fault once execution is quiescent and stopped. */
err_h vm_exec_fault_acknowledge(void);

/** @brief Set run mode directly. */
void vm_exec_set_mode(vm_run_mode_e mode);

/** @brief Get current run mode. */
vm_run_mode_e vm_exec_mode(void);

/** @brief Execute interactive execution command (0x48 packet). */
err_h vm_exec_control(vm_exec_command_e command);

/** @brief Query current supervisor execution state and debug status. */
vm_exec_status_t vm_exec_status(void);

/** @brief True if the current pass has a stop/reset/program-lock cancellation. */
bool vm_exec_cancelled(void);

/**
 * @brief Acquire the exclusive, quiescent barrier used for program mutation.
 * @param out_previous Receives the mode to restore if the mutation fails.
 * @return NULL with the barrier held, or ERR_VM_EXEC_SELF_BARRIER when called
 *         by the task currently executing a pass.
 */
err_h vm_exec_program_lock(vm_run_mode_e* out_previous);

/** @brief Release execution barrier with new run mode. */
void vm_exec_program_unlock(vm_run_mode_e mode);

/** @brief Reset executor state, event queues, overrides, and statistics. */
void vm_exec_reset(void);

/* ========================================================================= */
/* Pass Execution & Metrics                                                  */
/* ========================================================================= */

/**
 * @brief Execute a single pass across all loaded blocks.
 *
 * A non-supervisor caller may use this as an explicit manual pass while the
 * run mode is STOPPED, unless a stop request is pending. The supervisor stays
 * parked in STOPPED.
 */
void vm_exec_pass(void);

/** @brief Execute a contiguous range of blocks [start, end). Used for spans/loops. */
void vm_exec_run_range(uint16_t start, uint16_t end);

/** @brief Set telemetry hook called at end of each pass before clearing upd flags. */
void vm_exec_set_sample_hook(void (*hook)(void));

/** @brief Completed passes count since last stats reset. */
uint32_t vm_exec_pass_count(void);

/** @brief Wall duration of the last pass in microseconds. */
uint32_t vm_exec_last_pass_us(void);

typedef struct vm_exec_perf_t {
  uint32_t pass_count;
  uint32_t last_pass_us;
  uint32_t min_pass_us;
  uint32_t max_pass_us;
  uint64_t total_pass_us;
  uint32_t last_cycle_us;
  uint32_t min_cycle_us;
  uint32_t max_cycle_us;
  uint64_t total_cycle_us;
} vm_exec_perf_t;

/** @brief Retrieve comprehensive scan cycle performance metrics. */
vm_exec_perf_t vm_exec_get_perf(void);

/** @brief Reset pass metrics. */
void vm_exec_reset_stats(void);
