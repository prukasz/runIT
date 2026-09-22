#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_tps55289/include/device_tps55289.h"

//@id device_tps55289
//@version 1.0.0
//@title TPS55289 buck-boost regulator
//@description Adjustable I2C buck-boost voltage regulator - a programmable power output with a current limit and enable control.
//@protocol i2c
//@tags i2c power voltage current regulator
//@contract-provider $SYS_DEVICE_CONTRACT_POWER_VREG
//@property ALERT-SEVERITY @ref sys_power_events_e @one-of [$SYS_PWR_EVENT_OCP_CRITICAL, $SYS_PWR_EVENT_OCP_WARNING, $SYS_PWR_EVENT_OVP, $SYS_PWR_EVENT_UVP, $SYS_PWR_EVENT_OTP]

//@contract packet_sys_vreg_set_enable_t @alias Enable output
//@param state @alias Enabled @type bool

//@contract packet_sys_vreg_set_voltage_t @alias Set output voltage
//@param voltage_mV @alias Voltage @type uint32_t @unit mV

//@contract packet_sys_vreg_set_current_t @alias Set output current limit
//@param current_mA @alias Current Limit @type uint32_t @unit mA

//@contract packet_sys_vreg_add_callback_t @alias Configure power alert
//@param on_event @arg ALERT-SEVERITY @alias Alert Event
//@param route_mask @alias Alert Route Mask @type uint16_t @note Bitmask of callback routes (see SYS_CB_ROUTE_*) that should receive this alert event
//@description Fires when the output regulator trips a protection event (over-voltage, over-current, etc).

#define HEADER_packet_sys_device_install_tps55289_t 0x43
typedef struct __packed {
  uint8_t device_id; //@required @min 0 @max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;   //@required @min 0 @max 1
  uint8_t i2c_addr;  //@required @available [0x74, 0x75] @note enforced in tps55289_new() via TPS55289_I2C_ADDR_74/_75 - any other value fails at driver init
  uint8_t intr_pin_device_id; //@group interrupt-pin @role device_id
  uint8_t intr_pin_pin;       //@group interrupt-pin @role pin @sentinel SYS_GPIO_NONE
  uint8_t intr_pin_mode;      //@group interrupt-pin @role mode @ref sys_io_mode_e
  uint8_t en_pin_device_id; //@group enable-pin @role device_id
  uint8_t en_pin_pin;       //@group enable-pin @role pin @sentinel SYS_GPIO_NONE
  uint8_t en_pin_mode;      //@group enable-pin @role mode @ref sys_io_mode_e
} packet_sys_device_install_tps55289_t;

static inline err_h decoder_packet_sys_device_install_tps55289_t(packet_sys_device_install_tps55289_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing tps55289 (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_tps55289_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr,
                           .intr_pin = pin_ref_from_wire(packet->intr_pin_device_id, packet->intr_pin_pin, packet->intr_pin_mode),
                           .en_pin = pin_ref_from_wire(packet->en_pin_device_id, packet->en_pin_pin, packet->en_pin_mode)};
  return d_tps55289_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_TPS55289_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_tps55289_t, packet_sys_device_install_tps55289_t, decoder_packet_sys_device_install_tps55289_t)
