#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------------------
 * Error policy (SYS_DEVICE.MD "Error policy"): per instance, importance and an
 * action per severity, set with ERROR_POLICY. The policy finds the devices of a
 * reported chain, filters them and responds: CRITICAL suspends every device,
 * then the device's action for that severity runs (through the runner the app
 * sets, so sys_device doesn't know sys_actions).
 * ------------------------------------------------------------------------- */

/**
 * @brief How much a device's errors matter (SYS_DEVICE.MD "Error policy"): which severities reach the policy.
 * A device starts at LOW. Error severities themselves never change: this only filters the response.
 */
//#ref-enum @alias Device Error Importance
typedef enum {
  SYS_DEVICE_IMPORTANCE_NONE = 0,  //@alias Ignore errors @description Ignores all errors
  SYS_DEVICE_IMPORTANCE_LOW = 1,  //@alias Low @description Critical only (default)
  SYS_DEVICE_IMPORTANCE_MEDIUM = 2,  //@alias Medium @description High and critical
  SYS_DEVICE_IMPORTANCE_HIGH = 3,  //@alias High @description Medium, high and critical
  SYS_DEVICE_IMPORTANCE_CRITICAL = 4,  //@alias Critical @description Low, medium, high and critical
} sys_device_importance_e;

#define SYS_DEVICE_ERR_LEVELS 4u  // one action per severity LOW, MEDIUM, HIGH, CRITICAL

/** @brief ERROR_POLICY: the write takes it, the read answers it. */
typedef struct __attribute__((packed)) {
  uint8_t importance;                       /**< sys_device_importance_e */
  uint32_t actions[SYS_DEVICE_ERR_LEVELS];  /**< Record run for low, medium, high, critical (0: none) */
} sys_device_error_policy_t;

/**
 * @brief Responses the policy runs on the failing device itself, in place of a record (SYS_DEVICE.MD "Error policy"):
 * a value of `actions[]` in class 0xFF, which sys_actions keeps free of records. No record per device is needed.
 */
//#ref-defines device_response @alias Device Response @prefix SYS_DEVICE_RESPONSE_
#define SYS_DEVICE_RESPONSE_SUSPEND 0xFF000001u  //@alias Suspend this device @description Suspends the device that failed
#define SYS_DEVICE_RESPONSE_RESET   0xFF000002u  //@alias Reset this device @description Resets the device that failed to its power-on state

/** @brief Stage of a response, in ERR_DEV_FAULT_RESPONSE. */
//#ref-enum @alias Fault Response Stage
typedef enum {
  SYS_DEVICE_FAULT_STAGE_VM_STOP = 0,  //@alias Vm stop @description Stop the VM
  SYS_DEVICE_FAULT_STAGE_SUSPEND = 1,  //@alias Suspend @description Suspend all devices
  SYS_DEVICE_FAULT_STAGE_ACTION = 2,  //@alias Action @description Run configured action
} sys_device_fault_stage_e;

/**
 * @brief Runs action `action` (a sys_actions ID, u32) as a response; `chain` is the reported chain (borrowed), so the
 * runner can refuse an action whose own failure is in it. The app sets it (runit: sys_actions_request, queued on the
 * router).
 * @return A failure to start it, or NULL.
 */
typedef err_h (*sys_device_action_runner_f)(uint32_t action, err_h chain);

/**
 * @brief Stops the VM on a critical error (stage SYS_DEVICE_FAULT_STAGE_VM_STOP): runs before the devices are
 * suspended, so the program stops writing to them. `device_id` is the failing device or SYS_DEVICE_ID_NONE. The app
 * sets it (runit: vm_stop_on_fault); without one the VM is left to the safe state alone.
 */
typedef err_h (*sys_device_vm_stop_f)(uint16_t device_id);

/** @brief Sets the VM stop. Boot only. */
void sys_device_set_vm_stop(sys_device_vm_stop_f fn);

/** @brief Sets the action runner. Boot only. Without one, actions are not run (CRITICAL still suspends). */
void sys_device_set_action_runner(sys_device_action_runner_f fn);

/**
 * @brief Runs the policy on a reported chain (the app's error sink calls it). Walks from the outermost node: each
 * device (ERR_DEV_ID) once, at its first mention, with the severity from there down; a device at importance NONE
 * ends the walk (it and every deeper cause are ignored). Admitted: CRITICAL at any importance but NONE, else when
 * severity + importance > CRITICAL. An admitted error: CRITICAL suspends every device, then the device's action for
 * that severity runs (a device that no longer exists, a failed CREATE, gets the suspend only, for a critical error).
 * A critical node before any device suspends every device too. A chain holding
 * ERR_DEV_FAULT_RESPONSE, or one reported while a response runs on this task, is not handled (no loops).
 * @return The first response failure (owned; the others are freed), or NULL. Does not free `chain`.
 */
MUST_USE err_h sys_device_handle_error(err_h chain);

/** @brief The policy of device `id`, false if it isn't registered. Doesn't take the registry lock (a short critical
 *  section), so the error path can't wait on an op in progress. */
bool sys_device_error_policy_get(uint16_t id, sys_device_error_policy_t* out);

#ifdef __cplusplus
}
#endif
