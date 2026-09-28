#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_tca6424a/include/device_tca6424a.h"

//#device device_tca6424a
//  @title       TCA6424A GPIO expander
//  @description 24-bit I2C GPIO expander - adds extra digital input/output pins over I2C.
//  @protocol    i2c
//  @tags        i2c gpio io expander
//  @datasheet   https://www.ti.com/lit/ds/symlink/tca6424a.pdf
//  @contract-provider $SYS_DEVICE_CONTRACT_IO

//#self-property PIN
//  @one-of   [0..23]
//  @alias    Expander Pin

//#property PIN-MODE
//  @enum-ref sys_io_mode_e
//  @one-of   [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_OUTPUT_PUSH_PULL]

//#property INTR-MODE
//  @enum-ref sys_io_intr_mode_e
//  @one-of   [$SYS_IO_INTR_DISABLE, $SYS_IO_INTR_MODE_RISING_EDGE, $SYS_IO_INTR_MODE_FALLING_EDGE,
//             $SYS_IO_INTR_MODE_BOTH_EDGES]
//  @alias    Trigger Mode

//#contract packet_sys_io_reset_t
//  @alias       Reset pin
//  @description Releases the pin back to its unconfigured state.
//  @param pin   @arg PIN

//#contract packet_sys_io_set_mode_t
//  @alias       Configure pin mode
//  @description This expander only supports plain digital input or output - no pull resistors,
//               open-drain, ADC, or PWM.
//  @param pin   @arg PIN
//  @param mode  @arg PIN-MODE

//#contract packet_sys_io_configure_intr_t
//  @alias       Configure pin interrupt
//  @description Digital edge-triggered interrupt only - this expander has no ADC, so the
//               window-comparator trigger modes don't apply.
//  @param pin   @arg PIN
//  @param mode  @arg INTR-MODE

//#contract packet_sys_io_set_level_t
//  @alias       Set pin level
//  @param pin   @arg PIN
//  @param level @type bool

//#contract packet_sys_io_get_level_t
//  @alias       Read pin level
//  @param pin   @arg PIN
//  @returns     level

//#contract packet_sys_io_toggle_t
//  @alias       Toggle pin
//  @description Flip an output pin's current level.
//  @param pin   @arg PIN

#define HEADER_packet_sys_device_install_tca6424a_t 0x42
typedef struct __packed {
  uint8_t device_id;       //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;
  uint8_t i2c_addr;        //@alias I2C Address @one-of [0x22, 0x23] @note ADDR low selects 0x22; high selects 0x23.
  pin_ref_wire_t intr_pin; //@modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP]
  pin_ref_wire_t rst_pin;
} packet_sys_device_install_tca6424a_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_install_tca6424a_t(packet_sys_device_install_tca6424a_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing tca6424a (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_tca6424a_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr,
                           .intr_pin = pin_ref_from_wire(packet->intr_pin), .rst_pin = pin_ref_from_wire(packet->rst_pin)};
  err_h err = PIN_REFS_BELOW(cfg.device_id, cfg.intr_pin, cfg.rst_pin);
  if (err) return err;
  return d_tca6424a_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_TCA6424A_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_tca6424a_t, packet_sys_device_install_tca6424a_t, decoder_packet_sys_device_install_tca6424a_t)
