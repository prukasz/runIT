#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "sys_error.h"
#include "sys_hbridge.h"
#include "sys_event.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  /* --- Configuration (Top) --- */
  uint8_t bridge_device_id;            /* Underlying device ID implementing SYS_DEVICE_CONTRACT_HBRIDGE */
  uint8_t channel;                     /* Channel index on that device (e.g. 0 = Motor A, 1 = Motor B) */
  bool inverted;                       /* Invert direction */

  /* --- Runtime State (Below) --- */
  uint8_t feature_id;                  /* ID assigned to this feature */
  uint8_t fault_sub;                   /* sys_event subscription on the bridge channel */
  bool fault_subscribed;               /* fault_sub is valid */
  float current_speed;                 /* Last commanded speed (-1.0f to +1.0f) */
  int32_t last_current_mA;             /* Last sampled current in mA */
  sys_hbridge_fault_reason_e fault_reason; /* Reason for last detected fault */
  bool is_braking;                     /* True if in active brake state */
  bool is_coasting;                    /* True if in Hi-Z coast state */
  bool is_fault;                       /* True if fault was detected */
} feature_hbridge_t;

/**
 * @brief Creates and registers a new H-Bridge motor feature.
 *
 * Configures the underlying sys_hbridge channel into Full-Bridge mode,
 * listens to its faults and initializes output to brake (stopped). A fault is
 * republished as SYS_EVENT_DOMAIN_FEATURE (device = feature ID, event =
 * sys_hbridge_fault_reason_e, value = current in mA).
 *
 * @param feature_id Unique feature identifier.
 * @param config Configuration structure.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_hbridge_create(uint8_t feature_id, const feature_hbridge_t* config);

/**
 * @brief Sets motor speed and direction.
 *
 * @param feature_id Feature ID.
 * @param speed Range from -1.0f (full reverse) to +1.0f (full forward). 0.0f applies active brake.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_hbridge_set_speed(uint8_t feature_id, float speed);

/**
 * @brief Gets current speed setting.
 *
 * @param feature_id Feature ID.
 * @param out_speed Pointer to receive speed (-1.0f to +1.0f).
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_hbridge_get_speed(uint8_t feature_id, float* out_speed);

/**
 * @brief Actively brakes the motor (shorts motor terminals together).
 *
 * @param feature_id Feature ID.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_hbridge_brake(uint8_t feature_id);

/**
 * @brief Disables outputs to let the motor coast freely (Hi-Z).
 *
 * @param feature_id Feature ID.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_hbridge_coast(uint8_t feature_id);

/**
 * @brief Reads the load current in milliamps from the underlying bridge driver.
 *
 * @param feature_id Feature ID.
 * @param out_current_mA Pointer to receive load current in mA.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_hbridge_get_current_mA(uint8_t feature_id, int32_t* out_current_mA);

/**
 * @brief Configures current regulation limit in mA on the underlying driver.
 *
 * @param feature_id Feature ID.
 * @param limit_mA Current chopping limit in mA.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_hbridge_set_current_limit_mA(uint8_t feature_id, uint32_t limit_mA);

/**
 * @brief Checks if a hardware fault condition is present on this motor channel.
 *
 * @param feature_id Feature ID.
 * @param out_fault Pointer to receive fault state.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_hbridge_get_fault(uint8_t feature_id, bool* out_fault);

/**
 * @brief Gets detailed fault reason (overcurrent vs thermal/UVLO vs external).
 *
 * @param feature_id Feature ID.
 * @param out_reason Pointer to receive fault reason enum.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_hbridge_get_fault_reason(uint8_t feature_id, sys_hbridge_fault_reason_e* out_reason);

/**
 * @brief Clears a latched fault state on the motor channel.
 *
 * @param feature_id Feature ID.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_hbridge_clear_fault(uint8_t feature_id);

#ifdef __cplusplus
}
#endif
