#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_ina3221/include/device_ina3221.h"

//@id device_ina3221
//@version 1.0.0
//@title INA3221 power monitor
//@description Three-channel I2C bus-voltage and shunt-current monitor with warning and critical alerts.
//@protocol i2c
//@tags i2c power voltage current monitor alert ic
//@contract-provider $SYS_DEVICE_CONTRACT_POWER_MONITOR
//@self-property CHANNEL @one-of [0,1,2]
//@property ALERT-SEVERITY @ref sys_power_events_e @one-of [$SYS_PWR_EVENT_OCP_CRITICAL, $SYS_PWR_EVENT_OCP_WARNING]

//@contract packet_sys_power_monitor_get_voltage_t @alias Read bus voltage
//@param channel @arg CHANNEL @alias Monitor Channel
//@returns voltage_mV @type int32_t @unit mV

//@contract packet_sys_power_monitor_get_current_t @alias Read shunt current
//@param channel @arg CHANNEL @alias Monitor Channel
//@returns current_mA @type int32_t @unit mA

//@contract packet_sys_power_monitor_add_callback_t @alias Configure current alert
//@param channel @arg CHANNEL @alias Monitor Channel
//@param trigger_value @alias Current Threshold @type int32_t @unit mA
//@param on_event @arg ALERT-SEVERITY @alias Alert Severity
//@param route_mask @alias Alert Route Mask @type uint16_t @note Bitmask of callback routes (see SYS_CB_ROUTE_*) that should receive this alert event
//@description Configure the selected channel's critical or warning over-current alert and its event route.

#define HEADER_packet_sys_device_install_ina3221_t 0x44
typedef struct __packed {
  uint8_t device_id; //@required @min 0 @max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;   //@required @min 0 @max 1
  uint8_t i2c_addr;  //@required @min 0x40 @max 0x43 @note enforced in ina3221_new() against INA3221_I2C_ADDR_GND/_SCL - A0 pin strap (GND/Vs+/SDA/SCL) selects the address
  uint8_t crit_pin_device_id; //@group critical-alert-pin @role device_id
  uint8_t crit_pin_pin;       //@group critical-alert-pin @role pin @sentinel SYS_GPIO_NONE @note SYS_GPIO_NONE disables external critical-alert reporting; the pin is active-low
  uint8_t crit_pin_mode;      //@group critical-alert-pin @role mode @ref sys_io_mode_e
  uint8_t warn_pin_device_id; //@group warning-alert-pin @role device_id
  uint8_t warn_pin_pin;       //@group warning-alert-pin @role pin @sentinel SYS_GPIO_NONE @note SYS_GPIO_NONE disables external warning-alert reporting; the pin is active-low
  uint8_t warn_pin_mode;      //@group warning-alert-pin @role mode @ref sys_io_mode_e
} packet_sys_device_install_ina3221_t;

static inline err_h decoder_packet_sys_device_install_ina3221_t(packet_sys_device_install_ina3221_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing ina3221 (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_ina3221_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr,
                          .crit_pin = pin_ref_from_wire(packet->crit_pin_device_id, packet->crit_pin_pin, packet->crit_pin_mode),
                          .warn_pin = pin_ref_from_wire(packet->warn_pin_device_id, packet->warn_pin_pin, packet->warn_pin_mode)};
  return d_ina3221_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_INA3221_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_ina3221_t, packet_sys_device_install_ina3221_t, decoder_packet_sys_device_install_ina3221_t)
