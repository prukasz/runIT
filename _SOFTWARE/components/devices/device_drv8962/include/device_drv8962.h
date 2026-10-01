#pragma once
#include "sys_device.h"
#include "sys_error.h"
#include "drv8962_types.h"
#include "sys_hbridge.h"
#include "sys_io.h"

#define DRV8962_TYPE_ID 0x49  /* the byte after 0x00 in a create frame: [0x00][0x49][d_drv8962_cfg_t] */

/* Operations of the device (op byte of the router frame, [op][device_id][args]). Args are packed little endian. */
#define DRV8962_OP_SET_CURRENT_LIMIT 0x80 /* chopping limit of a channel: VREF follows the largest limit of the chip */
#define DRV8962_OP_CLEAR_FAULT 0x81       /* nSLEEP reset pulse clears the chip's fault latches; the channel brakes */
#define DRV8962_OP_GET_FAULT 0x82         /* read-only: the latched fault of a channel */

typedef struct __packed {
  uint8_t channel;
  uint32_t limit_mA; /* 0 = no limit */
} drv8962_set_current_limit_t;

typedef struct __packed {
  uint8_t channel;
} drv8962_channel_t;

typedef struct __packed {
  uint8_t fault;  /* 0 / 1 */
  uint8_t reason; /* sys_hbridge_fault_reason_e */
} drv8962_fault_t;

// The annotations below describe the device to the app (data-structures/devices/*.generated.json,
// grammar: data-structures/auto-annotations/device/device-annotations.md).

//#device device_drv8962
//  @title       DRV8962 motor driver
//  @description Dual full-bridge (or four half-bridge) motor driver controlled through the PWM and GPIO pins of other devices, with current sensing and a fault input.
//  @protocol    pwm
//  @tags        motor driver h-bridge pwm
//  @datasheet   https://www.ti.com/lit/ds/symlink/drv8962.pdf
//  @type-id     DRV8962_TYPE_ID
//  @contract-provider $SYS_DEVICE_CONTRACT_HBRIDGE

//#self-property CHANNEL
//  @one-of   [0..3]
//  @alias    Bridge Channel

//#contract packet_sys_hbridge_set_drive_t
//  @alias       Drive channel
//  @description Full bridge: -1.0 (reverse) to 1.0 (forward), slow decay. Half bridges: 0.0 to 1.0. A magnitude below 0.5 % brakes.
//  @param channel   @arg CHANNEL
//  @param magnitude @alias Drive Magnitude

//#contract packet_sys_hbridge_brake_t
//  @alias       Brake
//  @description Short the motor terminals (outputs enabled, inputs low).
//  @param channel   @arg CHANNEL

//#contract packet_sys_hbridge_coast_t
//  @alias       Coast
//  @description Release the motor: outputs high-Z.
//  @param channel   @arg CHANNEL

/**
 * @brief DRV8962 configuration. Also the wire struct of the create frame (packed, device_id first).
 *
 * The chip has no bus: every signal is a pin of another device. IN1..IN4 carry the PWM, EN1..EN4 enable
 * the half bridges, IPROPI1..IPROPI4 are the current sense outputs (ADC pins), nSLEEP wakes / resets the
 * chip, nFAULT reports faults and VREF (a DAC pin) sets the chopping current.
 *
 * @warning A pin that is not wired MUST be spelled `SYS_IO_PIN_NONE_INIT`; omitting the field
 *          zero-fills it to device 0 / pin 0, which is a real pin.
 */
typedef struct __packed {
  uint8_t device_id;     //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t topology;      //@alias Topology @one-of [$DRV8962_TOPOLOGY_2_FULL_BRIDGES, $DRV8962_TOPOLOGY_4_HALF_BRIDGES] @default 0
  sys_io_pin_ref_t in1_pin;     //@alias IN1 @modes [$SYS_IO_MODE_PWM] @default-mode $SYS_IO_MODE_PWM
  sys_io_pin_ref_t in2_pin;     //@alias IN2 @modes [$SYS_IO_MODE_PWM] @default-mode $SYS_IO_MODE_PWM
  sys_io_pin_ref_t in3_pin;     //@alias IN3 @modes [$SYS_IO_MODE_PWM] @default-mode $SYS_IO_MODE_PWM
  sys_io_pin_ref_t in4_pin;     //@alias IN4 @modes [$SYS_IO_MODE_PWM] @default-mode $SYS_IO_MODE_PWM
  sys_io_pin_ref_t en1_pin;     //@alias EN1 @modes [$SYS_IO_MODE_OUTPUT_PUSH_PULL] @default-mode $SYS_IO_MODE_OUTPUT_PUSH_PULL
  sys_io_pin_ref_t en2_pin;     //@alias EN2 @modes [$SYS_IO_MODE_OUTPUT_PUSH_PULL] @default-mode $SYS_IO_MODE_OUTPUT_PUSH_PULL
  sys_io_pin_ref_t en3_pin;     //@alias EN3 @modes [$SYS_IO_MODE_OUTPUT_PUSH_PULL] @default-mode $SYS_IO_MODE_OUTPUT_PUSH_PULL
  sys_io_pin_ref_t en4_pin;     //@alias EN4 @modes [$SYS_IO_MODE_OUTPUT_PUSH_PULL] @default-mode $SYS_IO_MODE_OUTPUT_PUSH_PULL
  sys_io_pin_ref_t ipropi1_pin; //@alias IPROPI1 @modes [$SYS_IO_MODE_ADC] @default-mode $SYS_IO_MODE_ADC
  sys_io_pin_ref_t ipropi2_pin; //@alias IPROPI2 @modes [$SYS_IO_MODE_ADC] @default-mode $SYS_IO_MODE_ADC
  sys_io_pin_ref_t ipropi3_pin; //@alias IPROPI3 @modes [$SYS_IO_MODE_ADC] @default-mode $SYS_IO_MODE_ADC
  sys_io_pin_ref_t ipropi4_pin; //@alias IPROPI4 @modes [$SYS_IO_MODE_ADC] @default-mode $SYS_IO_MODE_ADC
  sys_io_pin_ref_t nsleep_pin;  //@alias nSLEEP @modes [$SYS_IO_MODE_OUTPUT_PUSH_PULL] @default-mode $SYS_IO_MODE_OUTPUT_PUSH_PULL
  sys_io_pin_ref_t nfault_pin;  //@alias nFAULT @modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP] @default-mode $SYS_IO_MODE_INPUT_PULLUP
                                //  @note Open-drain, active-low.
  sys_io_pin_ref_t vref_pin;    //@alias VREF @modes [$SYS_IO_MODE_DAC] @default-mode $SYS_IO_MODE_DAC
                                //  @note Sets the chopping current from the largest channel limit. Without it the chip regulates at full scale.
  uint32_t pwm_freq_Hz;         //@alias PWM Frequency @unit Hz @min 0 @max 100000 @default 20000 @note 0 selects 20 kHz.
  uint32_t ripropi_ohms[4];     //@alias IPROPI Resistor @unit ohm @min 0 @max 100000 @note 0 selects 3090 ohm.
  uint32_t current_limit_mA[4]; //@alias Current Limit @unit mA @min 0 @max 10000 @note 0 disables the limit.
  uint8_t turn_off_at_ocp;      //@alias Brake on Overcurrent @min 0 @max 1 @default 1
} d_drv8962_cfg_t;

/** The DRV8962 device class: register with sys_device_register_class(), create with SYS_DEVICE_CREATE(&g_drv8962_class, &cfg). */
extern const sys_device_class_t g_drv8962_class;
