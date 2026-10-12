#pragma once

#include "sys_device.h"
#include "sys_hbridge.h"
#include "sys_io.h"

//#device
//@alias DRV8962 motor driver
//@description Dual full-bridge (or four half-bridge) motor driver controlled through the PWM and GPIO pins of other devices, with current sensing and a fault input.
//@protocol pwm
//@tags motor driver h-bridge pwm
//@datasheet https://www.ti.com/lit/ds/symlink/drv8962.pdf
//@pins HBRIDGE @one-of [0..3] @alias Bridge channel
//@event HBRIDGE_EV_OVERCURRENT HBRIDGE_EV_THERMAL_OR_UVLO HBRIDGE_EV_EXTERNAL @channel Bridge channel

#ifdef __cplusplus
extern "C" {
#endif

#define DRV8962_CURRENT_LIMIT_MAX_MA 10000u

//#ref-enum @alias DRV8962 Topology
typedef enum drv8962_topology_e {
  DRV8962_TOPOLOGY_2_FULL_BRIDGES = 0,  //@alias Two full bridges @description Channel 0 drives OUT1+OUT2 (motor A), channel 1 drives OUT3+OUT4 (motor B), both directions
  DRV8962_TOPOLOGY_4_HALF_BRIDGES = 1,  //@alias Four half bridges @description Channels 0..3 drive OUT1..OUT4 one by one (solenoids, single-direction loads)
} drv8962_topology_e;

/* The chip has no bus: every signal is a pin of another device. Indexes into drv8962_cfg_t.pin. IN1..IN4 carry the
   PWM, EN1..EN4 enable the half bridges, IPROPI1..IPROPI4 are the current sense outputs (ADC pins), nSLEEP wakes /
   resets the chip, nFAULT reports faults and VREF (a DAC pin) sets the chopping current. */
enum {
  DRV8962_PIN_IN1 = 0,
  DRV8962_PIN_EN1 = 4,
  DRV8962_PIN_IPROPI1 = 8,
  DRV8962_PIN_NSLEEP = 12,
  DRV8962_PIN_NFAULT = 13,
  DRV8962_PIN_VREF = 14,
  DRV8962_PINS = 15,
};

/**
 * @brief Create struct (packed, also the wire form):
 * CREATE = [0x05][id u16][op 0x0000][type u16 = SYS_DEVICE_TYPE_DRV8962][drv8962_cfg_t].
 *
 * @warning A pin that is not wired MUST be spelled `SYS_IO_PIN_NONE`: a zero-filled pin is device 0 / pin 0, a real
 * pin. Every pin's device must exist (sys_device_use: ERR_DEV_NOT_FOUND) and can't be removed while this one does.
 *
 * A full bridge uses half bridges 0+1 (channel 0) and 2+3 (channel 1): their IN / EN / IPROPI are those of the
 * half bridges, while `current_limit_mA` is per channel. The PWM frequency is set on every IN pin, so it must suit the
 * device behind it: the 20 kHz default is for ESP GPIO pins, a PCA9685 channel takes at most 1526 Hz.
 */
typedef struct __attribute__((packed)) {
  uint8_t topology;  //@alias Topology @description How the four half bridges are used @enum-ref drv8962_topology_e @default $DRV8962_TOPOLOGY_2_FULL_BRIDGES
  sys_io_pin_t pin[DRV8962_PINS];  //@alias Pins @description Every signal of the chip is a pin of another device @aliases IN1|IN2|IN3|IN4|EN1|EN2|EN3|EN4|IPROPI1|IPROPI2|IPROPI3|IPROPI4|nSLEEP|nFAULT|VREF @modes [$SYS_IO_MODE_PWM]*4|[$SYS_IO_MODE_OUTPUT_PUSH_PULL]*4|[$SYS_IO_MODE_ADC]*4|[$SYS_IO_MODE_OUTPUT_PUSH_PULL]|[$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP]|[$SYS_IO_MODE_DAC]
  uint32_t pwm_freq_Hz;  //@alias PWM frequency @description 0 selects 20 kHz; a PCA9685 channel takes at most 1526 Hz @unit Hz @min 0 @max 100000 @default 20000
  uint32_t ripropi_ohms[4];  //@alias IPROPI resistor @description The current-sense resistor of each half bridge; 0 selects 3090 @unit ohm @min 0 @max 100000
  uint32_t current_limit_mA[4];  //@alias Current limit @description Per channel; 0 means no limit @unit mA @min 0 @max DRV8962_CURRENT_LIMIT_MAX_MA
  uint8_t turn_off_at_ocp;  //@alias Brake on over-current @description A channel over its limit is braked @choices 0=No,1=Yes @default 1
} drv8962_cfg_t;

/* Own ops */
#define DRV8962_OP_SET_CURRENT_LIMIT SYS_DEVICE_OWN_OP(0x00)  // drv8962_limit_t: chopping limit of a channel; VREF follows the largest
#define DRV8962_OP_CLEAR_FAULT SYS_DEVICE_OWN_OP(0x01)        // hbridge_channel_args_t: nSLEEP reset pulse clears the chip's latches; the channel brakes
#define DRV8962_OP_GET_FAULT SYS_DEVICE_OWN_OP(0x02)          // hbridge_channel_args_t -> drv8962_fault_t

typedef struct __attribute__((packed)) {
  uint8_t channel;  //@alias Channel @description The motor channel @id pin
  uint32_t limit_mA;  //@alias Limit @description 0 means no limit @unit mA
} drv8962_limit_t;

typedef struct __attribute__((packed)) {
  uint8_t fault;  //@alias Fault @choices 0=No,1=Yes
  uint8_t reason;  //@alias Reason @enum-ref sys_hbridge_fault_reason_e
} drv8962_fault_t;

#ifdef __cplusplus
}
#endif
