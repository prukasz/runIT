#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "errors.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The device error policy, beside the registry: per device, how much its errors matter (importance) and an
 * action per severity. The policy finds the devices of a reported chain, filters them and responds: CRITICAL enters the
 * board's safe state (sys_system_enter_safe_state, reason FAULT: the event SAFE stops the VM, then every device is
 * suspended), then the device's action for that severity runs (a built-in response, or a stored action queued with
 * sys_actions_request; never the action whose own step failed in the chain). The responses run in the policy's own task, never in the task that reported
 * the error: that one may be inside a device op, holding the lock the response needs. A device gets its policy when it
 * is created and loses it when it is removed (the registry's events CREATED / REMOVED); the registry knows nothing of
 * it. Commands: root key MODULE_ID_POLICY (sys_device_policy_router.h).
 */

/**
 * @brief How much a device's errors matter: which severities reach the policy. A device starts at LOW. Error
 * severities themselves never change: this only filters the response.
 */
//#ref-enum @alias Device Error Importance
typedef enum {
  SYS_DEVICE_IMPORTANCE_NONE = 0,      //@alias Ignore errors @description Ignores all errors
  SYS_DEVICE_IMPORTANCE_LOW = 1,       //@alias Low @description Critical only (default)
  SYS_DEVICE_IMPORTANCE_MEDIUM = 2,    //@alias Medium @description High and critical
  SYS_DEVICE_IMPORTANCE_HIGH = 3,      //@alias High @description Medium, high and critical
  SYS_DEVICE_IMPORTANCE_CRITICAL = 4,  //@alias Critical @description Low, medium, high and critical
} sys_device_importance_e;

#define SYS_DEVICE_ERR_LEVELS 4u  // one action per severity LOW, MEDIUM, HIGH, CRITICAL

/** @brief A device's error policy: what the POLICY command writes and answers. */
typedef struct __attribute__((packed)) {
  uint8_t importance;                       /**< sys_device_importance_e */
  uint32_t actions[SYS_DEVICE_ERR_LEVELS];  /**< Run for low, medium, high, critical: an action ID, a response, 0 */
} sys_device_error_policy_t;

/**
 * @brief Responses the policy runs on the failing device itself, in place of a record: a value of `actions[]` in class
 * 0xFF, which sys_actions keeps free of records.
 */
//#ref-defines device_response @alias Device Response @prefix SYS_DEVICE_RESPONSE_
#define SYS_DEVICE_RESPONSE_SUSPEND 0xFF000001u  //@alias Suspend this device @description Suspends the device that failed
#define SYS_DEVICE_RESPONSE_RESET   0xFF000002u  //@alias Reset this device @description Resets the device that failed to its power-on state

/** @brief Stage of a response, in ERR_DEV_FAULT_RESPONSE. */
//#ref-enum @alias Fault Response Stage
typedef enum {
  SYS_DEVICE_FAULT_STAGE_SAFE = 0,    //@alias Safe state @description Enter the safe state (the VM stopped, every device suspended)
  SYS_DEVICE_FAULT_STAGE_ACTION = 1,  //@alias Action @description Run configured action
  SYS_DEVICE_FAULT_STAGE_QUEUE = 2,   //@alias Queue @description Hand the error to the policy's task
} sys_device_fault_stage_e;

/**
 * @brief Hands a reported chain to the policy (the app's error sink calls it, on the task that reported). Nothing
 * runs here: a chain that needs a response is copied as a record (ERR_RECORD_MAX, err_record.h) into the queue
 * of the policy's task, which runs the policy on it and reports a response's failure itself.
 * The policy walks from the outermost node: each device (ERR_DEV_ID) once, at its first mention, with the severity
 * from there down; a device at importance NONE ends the walk (it and every deeper cause are ignored). Admitted:
 * CRITICAL at any importance but NONE, else when severity + importance > CRITICAL. An admitted error: CRITICAL enters
 * the safe state (reason FAULT, source the device), then the device's action for that severity runs (a device with no
 * policy, a failed CREATE, gets the safe state only, for a critical error). A critical node before any device enters
 * the safe state too (source SYS_DEVICE_ID_NONE).
 * A chain holding ERR_DEV_FAULT_RESPONSE, or one reported by the policy's task (while a response runs), is not handled
 * (no loops).
 * @param[in] chain The reported error chain, not freed here and not kept
 * @return NULL when the chain was queued or needs no response; ERR_DEV_FAULT_RESPONSE{stage QUEUE} (owned) when the
 *         queue had no room: the response does not run
 */
MUST_USE err_h sys_device_policy_handle(err_h chain);

/**
 * @brief The policy of device `id`. A short critical section, no registry lock, so the error path can't wait on an op
 * in progress.
 * @param[in]  id  Device ID
 * @param[out] out The device's error policy
 * @return false when the device has none (it doesn't exist)
 */
bool sys_device_policy_get(uint8_t id, sys_device_error_policy_t* out);

/**
 * @brief Sets the policy of device `id`.
 * @param[in] id Device ID
 * @param[in] p  The policy: importance NONE..CRITICAL, any action ID, a known response
 * @return ERR_RANGE (importance), ERR_BAD_ARG (an unknown response), ERR_DEV_NOT_FOUND
 */
MUST_USE err_h sys_device_policy_set(uint8_t id, const sys_device_error_policy_t* p);

#ifdef __cplusplus
}
#endif
