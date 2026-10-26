#pragma once

#include "sys_device.h"
#include "sys_event.h"
#include "sys_hbridge.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A brushed DC motor on one channel of a motor driver (any device with the HBRIDGE contract: the DRV8962). The
 * motor is what a program drives: a speed in percent that it reaches along a ramp, never in one jump; through zero when
 * the direction changes (a pause there spares the gearbox); with a dead zone round zero, a speed where the motor starts
 * turning, a ceiling (a 6 V motor on 12 V: 50 %), a trim that matches it to its pair and a reversed direction. The
 * bridge stays the raw output: this device is the motor's behaviour.
 *
 * Speed is in tenths of a percent on the wire: -1000 (full reverse) .. 1000 (full forward); 0..1000 for a motor on a
 * half bridge (`one_way`). The output (where the ramp is) maps onto the drive: 0 stops (brake or coast, `stop_mode`),
 * anything else is min_pct + |speed| x (max_pct - min_pct), scaled by the trim, never over max_pct.
 */

//#ref-enum @alias DC Motor Stop
typedef enum dc_motor_stop_e {
  DC_MOTOR_STOP_BRAKE = 0,  //@alias Brake @description Shorts the motor: it stops fast and holds
  DC_MOTOR_STOP_COAST = 1,  //@alias Coast @description Releases the motor: it rolls to a stop
} dc_motor_stop_e;

#define DC_MOTOR_SPEED_MAX        1000   // tenths of a percent: 100 %
#define DC_MOTOR_MIN_PCT_MAX      60
#define DC_MOTOR_DEADZONE_PCT_MAX 20
#define DC_MOTOR_TRIM_PCT_MAX     20
#define DC_MOTOR_RAMP_MS_MAX      10000
#define DC_MOTOR_PAUSE_MS_MAX     2000
#define DC_MOTOR_STALL_MS_MIN     50
#define DC_MOTOR_STALL_MS_MAX     5000

/* The settings that change live: create takes them after the motor's bridge and channel, SETTINGS reads and writes
   them with the same names. */
typedef struct __attribute__((packed)) {
  uint8_t invert;             /**< 1 = forward and reverse swapped (instead of swapping the wires) */
  uint8_t stop_mode;          /**< What 0 does: dc_motor_stop_e */
  uint8_t max_pct;            /**< Highest drive, 1..100 % (a 6 V motor on 12 V: 50) */
  uint8_t min_pct;            /**< Drive where the motor starts turning, 0..60 %: any speed past the dead zone starts here */
  uint8_t deadzone_pct;       /**< A speed under it is 0, 0..20 % (joystick or slider noise) */
  int8_t trim_pct;            /**< Drive scaled by 100 + trim percent, -20..20 (match the motor to its pair) */
  uint16_t ramp_up_ms;        /**< 0 to full speed, 0..10000 ms (0 = at once) */
  uint16_t ramp_down_ms;      /**< Full speed to 0, 0..10000 ms (0 = at once) */
  uint16_t reverse_pause_ms;  /**< Stopped this long between the directions, 0..2000 ms */
  uint16_t stall_mA;          /**< A current at or over it for stall_ms is a stall: the motor stops (0 = not watched) */
  uint16_t stall_ms;          /**< How long the stall current lasts before the motor stops, 50..5000 ms */
} dc_motor_settings_t;

/**
 * @brief DC motor configuration struct (wire format for CREATE).
 */
typedef struct __attribute__((packed)) {
  uint16_t bridge;               /**< The motor driver (a device with the HBRIDGE contract) */
  uint8_t channel;               /**< Its channel the motor is on */
  uint8_t one_way;               /**< 1 = a half bridge: forward only */
  dc_motor_settings_t settings;  /**< The settings that change live */
} dc_motor_cfg_t;

/* Own ops */
#define DC_MOTOR_OP_SPEED    SYS_DEVICE_OWN_OP(0x00)  // dc_motor_speed_t, both sides: the speed to reach (the read answers it)
#define DC_MOTOR_OP_OUTPUT   SYS_DEVICE_OWN_OP(0x01)  // -> i32 tenths of a percent: where the ramp is now
#define DC_MOTOR_OP_STOP     SYS_DEVICE_OWN_OP(0x02)  // to 0 along the ramp, then stop_mode
#define DC_MOTOR_OP_BRAKE    SYS_DEVICE_OWN_OP(0x03)  // at once, no ramp: shorts the motor
#define DC_MOTOR_OP_COAST    SYS_DEVICE_OWN_OP(0x04)  // at once, no ramp: releases the motor
#define DC_MOTOR_OP_SETTINGS SYS_DEVICE_OWN_OP(0x05)  // dc_motor_settings_t, both sides: the live settings
#define DC_MOTOR_OP_CURRENT  SYS_DEVICE_OWN_OP(0x06)  // -> i32 mA: the bridge channel's current
#define DC_MOTOR_OP_STALLED  SYS_DEVICE_OWN_OP(0x07)  // -> u8: 1 = stopped by a stall since the last SPEED
#define DC_MOTOR_OP_TICK     SYS_DEVICE_OWN_OP(0x08)  // the ramp's timer, internal: one step of the ramp

typedef struct __attribute__((packed)) {
  int16_t speed_x10;  /**< Tenths of a percent: -1000 (full reverse) .. 1000 (full forward) */
} dc_motor_speed_t;

/* The device's own events (0xFFxx, SYS_EVENT.MD): a stall stopped the motor; channel 0, value = the current in mA. */
enum { DC_MOTOR_CONTRACT = 0xFF };
#define DC_MOTOR_EVENTS(X, P) \
  X(P, STALL, 0x00, SYS_EVENT_NOTIFY)
SYS_EVENTS_DECLARE(DC_MOTOR)

#ifdef __cplusplus
}
#endif
