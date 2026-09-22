#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_tca6424a/include/device_tca6424a.h"

//@id device_tca6424a
//@version 1.0.0
//@title TCA6424A GPIO expander
//@description 24-bit I2C GPIO expander - adds extra digital input/output pins over I2C.
//@protocol i2c
//@tags i2c gpio io expander
//@contract-provider $SYS_DEVICE_CONTRACT_IO
//@self-property PIN @one-of [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23]
//@property PIN-MODE @ref sys_io_mode_e @one-of [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_OUTPUT_PUSH_PULL]
//@property INTR-MODE @ref sys_io_intr_mode_e @one-of [$SYS_IO_INTR_DISABLE, $SYS_IO_INTR_MODE_RISING_EDGE, $SYS_IO_INTR_MODE_FALLING_EDGE, $SYS_IO_INTR_MODE_BOTH_EDGES]

//@contract packet_sys_io_reset_t @alias Reset pin
//@param pin @arg PIN @alias Expander Pin
//@description Releases the pin back to its unconfigured state.

//@contract packet_sys_io_set_mode_t @alias Configure pin mode
//@param pin @arg PIN @alias Expander Pin
//@param mode @arg PIN-MODE @alias Pin Mode
//@description This expander only supports plain digital input or output - no pull resistors, open-drain, ADC, or PWM.

//@contract packet_sys_io_configure_intr_t @alias Configure pin interrupt
//@param pin @arg PIN @alias Expander Pin
//@param mode @arg INTR-MODE @alias Trigger Mode
//@param route_mask @alias Interrupt Route Mask @type uint16_t @note Bitmask of callback routes (see SYS_CB_ROUTE_*) that should receive this interrupt event
//@description Digital edge-triggered interrupt only - this expander has no ADC, so the window-comparator trigger modes don't apply.

//@contract packet_sys_io_set_level_t @alias Set pin level
//@param pin @arg PIN @alias Expander Pin
//@param level @alias Level @type bool

//@contract packet_sys_io_get_level_t @alias Read pin level
//@param pin @arg PIN @alias Expander Pin
//@returns level @type bool

//@contract packet_sys_io_toggle_t @alias Toggle pin
//@param pin @arg PIN @alias Expander Pin
//@description Flip an output pin's current level.

#define HEADER_packet_sys_device_install_tca6424a_t 0x42
typedef struct __packed {
  uint8_t device_id; //@required @min 0 @max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;   //@required @min 0 @max 1
  uint8_t i2c_addr;  //@required @note not range-checked by driver_tca6424a.c - no software-enforced bound
  uint8_t intr_pin_device_id; //@group interrupt-pin @role device_id
  uint8_t intr_pin_pin;       //@group interrupt-pin @role pin @sentinel SYS_GPIO_NONE
  uint8_t intr_pin_mode;      //@group interrupt-pin @role mode @ref sys_io_mode_e
  uint8_t rst_pin_device_id; //@group reset-pin @role device_id
  uint8_t rst_pin_pin;       //@group reset-pin @role pin @sentinel SYS_GPIO_NONE
  uint8_t rst_pin_mode;      //@group reset-pin @role mode @ref sys_io_mode_e
} packet_sys_device_install_tca6424a_t;

static inline err_h decoder_packet_sys_device_install_tca6424a_t(packet_sys_device_install_tca6424a_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing tca6424a (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_tca6424a_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr,
                           .intr_pin = pin_ref_from_wire(packet->intr_pin_device_id, packet->intr_pin_pin, packet->intr_pin_mode),
                           .rst_pin = pin_ref_from_wire(packet->rst_pin_device_id, packet->rst_pin_pin, packet->rst_pin_mode)};
  return d_tca6424a_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_TCA6424A_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_tca6424a_t, packet_sys_device_install_tca6424a_t, decoder_packet_sys_device_install_tca6424a_t)
