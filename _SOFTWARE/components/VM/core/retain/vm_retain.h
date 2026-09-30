#pragma once
#include <sdkconfig.h>
#include "sys_error.h"
#include "vm_obj.h"

/**
 * @file vm_retain.h
 * @brief Retentive objects: values that survive a reboot or a program re-upload.
 *
 * An object uploaded with the `retentive` flag must be named; its value is kept
 * under that name, like a PLC retain variable, so an edited program keeps it as
 * long as the name, type and size still match.
 *
 * - **Save:** at the end of a completed pass, every CONFIG_VM_RETAIN_PERIOD_S,
 *   the VM task captures all retentive objects into a staging buffer (one
 *   consistent snapshot). If anything changed since the last save, the retain
 *   task writes the whole set as one record (sys_settings key "vm_retain") —
 *   flash is never written on the VM task. A program is also saved right before
 *   it is torn down (0x40, 0x41) and when runit enters the safe state.
 * - **Restore:** on the first start after a load (a 0x48 run command), before the
 *   first pass: each stored value goes into the retentive object with the same
 *   name, type and size; a mismatch keeps the uploaded value and is reported.
 *   Restored values are not marked fresh, exactly like uploaded initial values.
 * - **Active window:** saving only happens between a program's start and its
 *   teardown, so an empty or unstarted program never overwrites stored values.
 * - **Clear (reset all):** vm_retain_clear() erases the store and stops saving
 *   until the next start, so the next start begins from the uploaded values.
 *
 * Record: [u16 count] then count x {u8 name_len, char name[name_len], u8 type,
 * u16 size, u8 data[size]}, little-endian. Everything a program retains must
 * fit CONFIG_VM_RETAIN_MAX_BYTES; the loader checks it per object.
 */

/** @brief Start the retain task. Boot step, after sys_settings_init(). */
SE_MUST_USE err_h vm_retain_init(void);

/** @brief New program: reset the size budget and arm the restore for its first start. */
void vm_retain_on_open(void);

/** @brief Program unloaded: nothing to save or restore until the next open. */
void vm_retain_on_reset(void);

/**
 * @brief Load-time check of one object with the retentive flag: it must be named
 * and its record must fit what is left of CONFIG_VM_RETAIN_MAX_BYTES.
 * No-op for objects without the flag.
 *
 * @return NULL, ERR_VM_RETAIN_UNNAMED, or ERR_VM_RETAIN_TOO_BIG.
 */
SE_MUST_USE err_h vm_retain_reserve(uint16_t obj_id, const vm_obj_head_t* head);

/**
 * @brief Restore stored values into the loaded program, once, on its first start.
 * Call with the VM stopped. Later calls are no-ops until the next open.
 */
SE_MUST_USE err_h vm_retain_restore(void);

/**
 * @brief End of a completed pass (VM task): capture on the period, hand changes
 * to the retain task.
 */
void vm_retain_on_pass(uint64_t now_ms);

/**
 * @brief Capture now and hand it to the retain task. The caller guarantees the
 * VM is quiescent (under the program lock, or after vm_exec_stop()).
 */
void vm_retain_save_stopped(void);

/**
 * @brief Reset all: erase the stored values and stop saving until the next
 * program start (0x48 command VM_EXEC_RETAIN_CLEAR).
 */
SE_MUST_USE err_h vm_retain_clear(void);
