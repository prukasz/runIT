#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "sys_error.h"
#include "sys_io.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  /* --- Configuration (Top) --- */
  uint8_t device_id;             /* Underlying device providing PWM (e.g. PCA9685) */
  sys_io_pin_num_t pin_num;      /* Pin / channel number on that device */
  uint16_t frequency_Hz;         /* PWM frequency, typically 50 Hz */
  uint16_t pulse_min_us;         /* Pulse width for angle_min (e.g. 500 us) */
  uint16_t pulse_max_us;         /* Pulse width for angle_max (e.g. 2500 us) */
  float angle_min;               /* Minimum logical angle (e.g. 0.0f or -90.0f) */
  float angle_max;               /* Maximum logical angle (e.g. 180.0f or +90.0f) */
  float default_angle;           /* Home/boot position in degrees */
  float trim_angle;              /* Calibration offset in degrees */
  bool inverted;                 /* Invert direction */

  /* --- Runtime State (Below) --- */
  float last_angle;              /* Last commanded logical angle */
  bool is_attached;              /* Active PWM output flag */
} feature_servo_t;

/**
 * @brief Creates and registers a new Servo feature in the registry.
 *
 * Configures the backing PWM pin frequency and sets initial home position.
 *
 * @param feature_id Unique feature ID.
 * @param config Pointer to the servo configuration struct.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_servo_create(uint8_t feature_id, const feature_servo_t* config);

/**
 * @brief Sets the logical angle for the servo (incorporating trim and clamping).
 *
 * @param feature_id Feature ID.
 * @param angle Target angle in degrees.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_servo_set_angle(uint8_t feature_id, float angle);

/**
 * @brief Gets the current/last commanded logical angle of the servo.
 *
 * @param feature_id Feature ID.
 * @param out_angle Pointer to receive current angle in degrees.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_servo_get_angle(uint8_t feature_id, float* out_angle);

/**
 * @brief Sets the calibration trim offset angle.
 *
 * @param feature_id Feature ID.
 * @param trim_angle Offset in degrees added to commanded angles.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_servo_set_trim(uint8_t feature_id, float trim_angle);

/**
 * @brief Gets the current calibration trim offset angle.
 *
 * @param feature_id Feature ID.
 * @param out_trim_angle Pointer to receive trim angle in degrees.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_servo_get_trim(uint8_t feature_id, float* out_trim_angle);

/**
 * @brief Moves the servo immediately to its default (home) angle.
 *
 * @param feature_id Feature ID.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_servo_go_home(uint8_t feature_id);

/**
 * @brief Attaches/enables the PWM output for the servo.
 *
 * @param feature_id Feature ID.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_servo_attach(uint8_t feature_id);

/**
 * @brief Detaches/disables PWM output (stops pulsing, releases holding torque).
 *
 * @param feature_id Feature ID.
 * @return err_h NULL on success, or sys_error handle.
 */
SE_MUST_USE err_h feature_servo_detach(uint8_t feature_id);

#ifdef __cplusplus
}
#endif

