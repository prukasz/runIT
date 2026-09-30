#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_tps55289/include/device_tps55289.h"

//#device device_tps55289
//  @title       TPS55289 buck-boost regulator
//  @description Adjustable I2C buck-boost voltage regulator - a programmable power output with a current
//               limit and enable control.
//  @protocol    i2c
//  @tags        i2c power voltage current regulator
//  @datasheet   https://www.ti.com/lit/ds/symlink/tps55289.pdf
//  @contract-provider $SYS_DEVICE_CONTRACT_POWER_VREG

//#contract packet_sys_power_vreg_set_enable_t
//  @alias       Enable output
//  @param state @alias Enabled @type bool

//#contract packet_sys_power_vreg_set_voltage_t
//  @alias       Set output voltage
//  @param voltage_mV

//#contract packet_sys_power_vreg_set_current_t
//  @alias       Set output current limit
//  @param current_mA

#define HEADER_packet_sys_device_install_tps55289_t 0x43
typedef struct __packed {
  uint8_t device_id;       //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;
  uint8_t i2c_addr;        //@alias I2C Address @one-of [0x74, 0x75] @note The MODE pin selects one of two I2C addresses.
  pin_ref_wire_t intr_pin; //@modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP]
  pin_ref_wire_t en_pin;
} packet_sys_device_install_tps55289_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_install_tps55289_t(packet_sys_device_install_tps55289_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing tps55289 (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_tps55289_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr,
                           .intr_pin = pin_ref_from_wire(packet->intr_pin), .en_pin = pin_ref_from_wire(packet->en_pin)};
  err_h err = PIN_REFS_BELOW(cfg.device_id, cfg.intr_pin, cfg.en_pin);
  if (err) return err;
  return d_tps55289_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_TPS55289_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_tps55289_t, packet_sys_device_install_tps55289_t, decoder_packet_sys_device_install_tps55289_t)
