#pragma once
#include "sys_device.h"
#include "sys_error.h"
#include "sys_io.h"

#define SERVO_TYPE_ID 0x48  /* the byte after 0x00 in a create frame: [0x00][0x48][d_servo_cfg_t] */

//#device device_servo
//  @title       Servo
//  @description Hobby servo on a PWM pin of another device. Go home, move to an angle, trim with an offset.
//  @protocol    pwm
//  @tags        servo pwm motion
//  @type-id     SERVO_TYPE_ID

/**
 * @brief Hobby servo on one PWM pin of another device (PCA9685 channel, ESP LEDC pin).
 *
 * The servo has no contract of its own: its functions are operations of the device,
 * called through the packet router as [op][device_id][args] (SYS_DEVICE.MD, Packet Router).
 * It drives the pin only through the IO contract, so any PWM-capable device works.
 * The cfg is also the wire struct of the create frame (packed, device_id first).
 * Nothing is sent to the servo until the first operation (it stays limp after install).
 */
typedef struct __packed {
  uint8_t device_id;      //@max CONFIG_SYS_DEVICE_MAX_ID
  sys_io_pin_ref_t pwm_pin; //@alias PWM Pin @note Any pin that can output PWM: a PCA9685 channel or an ESP GPIO. On a lower-ID device.
  uint16_t home_us;       //@alias Home Pulse @unit us @min 500 @max 2500 @default 1500
  uint16_t min_us;        //@alias Pulse at 0 degrees @unit us @min 500 @max 2500 @default 500 @note Also the lowest pulse ever sent.
  uint16_t max_us;        //@alias Pulse at 180 degrees @unit us @min 500 @max 2500 @default 2500 @note Also the highest pulse ever sent.
} d_servo_cfg_t;

/* Operations (op byte of the router frame). Args are packed little endian. */
#define SERVO_OP_HOME 0x80        /* go to home_us + offset */
#define SERVO_OP_SET_OFFSET 0x81  /* offset_us added to every target; applied by the next move */
#define SERVO_OP_SET_ANGLE 0x82   /* 0..180 degrees, linear between min_us and max_us */
#define SERVO_OP_GET_PULSE 0x83   /* read-only: the pulse last sent, uint16 us (0 = not driven) */

typedef struct __packed {
  int16_t offset_us; /* -500..500 */
} servo_set_offset_t;

typedef struct __packed {
  uint8_t angle_deg; /* 0..180 */
} servo_set_angle_t;

/* Ops without arguments */
typedef struct __packed {
} servo_no_args_t;

/** The servo device class: register with sys_device_register_class(), create with SYS_DEVICE_CREATE(&g_servo_class, &cfg). */
extern const sys_device_class_t g_servo_class;
