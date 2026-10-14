#pragma once
#include "errors.h"
#include "vm_err.h"
#include "vm_block.h"
#include "vm_clock.h"  // vm_now_ms(), vm_clock_us(): what the timing blocks read
#include "vm_stats.h"


/* ========================================================================= */
/* Execution Modes & Control Commands                                        */
/* ========================================================================= */

//#ref-enum @alias Program Run Mode
typedef enum vm_run_mode_e {
  VM_RUN_STOPPED    = 0,  //@alias Stopped @description Supervisor idle; explicit non-supervisor passes remain available
  VM_RUN_RUNNING    = 1,  //@alias Running @description Continuous cyclic passes (1 pass per tick)
  VM_RUN_FROZEN     = 2,  //@alias Frozen @description Paused before next block dispatch
  VM_RUN_STEP       = 3,  //@alias Step @description Run single pass, then transition to FROZEN
  VM_RUN_SCAN       = 4,  //@alias Scan @description Scan mode; waits for VM_EXEC_ONCE
  VM_RUN_BLOCK      = 5,  //@alias Block @description Block mode; waits for VM_EXEC_NEXT
  VM_RUN_BLOCK_STEP = 6,  //@alias Block step @description Execute single pending block dispatch
} vm_run_mode_e;

/** @brief String representation of VM run mode for debugging. */
static inline const char* vm_run_mode_str(vm_run_mode_e m) {
  return vm_run_mode_name((uint8_t)m);
}

//#ref-enum @alias VM Exec Command
typedef enum vm_exec_command_e {
  VM_EXEC_SCAN_MODE      = 0,  //@alias Scan mode @description Scan mode
  VM_EXEC_ONCE           = 1,  //@alias Once @description Once
  VM_EXEC_BLOCK_MODE     = 2,  //@alias Block mode @description Block mode
  VM_EXEC_NEXT           = 3,  //@alias Next @description Next
  VM_EXEC_RESET_TO_START = 4,  //@alias Reset to start @description Reset to start
  VM_EXEC_NORMAL_MODE    = 5,  //@alias Normal mode @description Normal mode
  VM_EXEC_PAUSE          = 6,  //@alias Pause @description Pause
  VM_EXEC_RESUME         = 7,  //@alias Resume @description Resume
  VM_EXEC_RESET          = 8,  //@alias Reset @description Reset
  VM_EXEC_ACK_FAULT      = 9,  //@alias Ack fault @description Ack fault
  VM_EXEC_RETAIN_CLEAR   = 10,  //@alias Retain clear @description Forget retained values (vm_retain_clear)
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
  bool     latched;
  uint16_t device_id;  // the device whose critical error it was; VM_FAULT_NO_DEVICE if none
  uint32_t root_tag;   // the chain's root cause (an ERR_TAG value)
  uint32_t occurrences;
} vm_exec_fault_status_t;

#include "vm_blocks.h"

/* ========================================================================= */
/* Palette Dispatch                                                          */
/* ========================================================================= */

/** @brief Validate block_type against the palette at load time. */
MUST_USE err_h vm_exec_check_block_type(uint16_t blk_id, uint8_t block_type);

/* ========================================================================= */
/* Supervisor Task & Lifecycle                                               */
/* ========================================================================= */

/** @brief Start the supervisor FreeRTOS task on core 1. Idempotent. */
MUST_USE err_h vm_exec_start(void);

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
MUST_USE err_h vm_exec_stop(void);

/**
 * @brief Latch a critical device fault and request cancellation immediately.
 * @return true only for the first fault in the current latched episode.
 */
bool vm_exec_fault_latch(uint16_t device_id, uint32_t root_tag);

#define VM_FAULT_NO_DEVICE 0xFFFFu

/** @brief Copy the persistent critical-fault snapshot. */
vm_exec_fault_status_t vm_exec_fault_status(void);

/** @brief Clear a latched fault once execution is quiescent and stopped. */
MUST_USE err_h vm_exec_fault_acknowledge(void);

/** @brief Set run mode directly. */
void vm_exec_set_mode(vm_run_mode_e mode);

/** @brief Get current run mode. */
vm_run_mode_e vm_exec_mode(void);

/** @brief Execute interactive execution command (0x48 packet). */
MUST_USE err_h vm_exec_control(vm_exec_command_e command);

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
MUST_USE err_h vm_exec_program_lock(vm_run_mode_e* out_previous);

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

/**
 * @brief Queues record @p id (a sys_actions ID, u32) to run on a router task.
 *
 * A block never runs a record on the VM task: it may rewind or unload the VM itself, and a replay outlasts the
 * block watchdog. This only queues (sys_actions_request), so it is cheap and never blocks.
 * @return NULL when queued, or the queue's error (ERR_ROUTER_QUEUE_FULL).
 */
MUST_USE err_h vm_exec_request_action(uint32_t id);

/* Pass and cycle statistics (vm_exec_get_perf, ...) are in vm_stats.h. */
