#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_ina3221/include/device_ina3221.h"

//#device device_ina3221
//  @title       INA3221 power monitor
//  @description Three-channel I2C bus-voltage and shunt-current monitor with warning and critical alerts.
//  @protocol    i2c
//  @tags        i2c power voltage current monitor alert ic
//  @datasheet   https://www.ti.com/lit/ds/symlink/ina3221.pdf
//  @contract-provider $SYS_DEVICE_CONTRACT_POWER_MONITOR

//#self-property CHANNEL
//  @one-of   [0..2]
//  @alias    Monitor Channel

//#property ALERT-SEVERITY
//  @enum-ref sys_power_events_e
//  @one-of   [$SYS_PWR_EVENT_OCP_CRITICAL, $SYS_PWR_EVENT_OCP_WARNING]
//  @alias    Alert Severity

//#contract packet_sys_power_monitor_get_voltage_t
//  @alias         Read bus voltage
//  @param channel @arg CHANNEL
//  @returns       voltage_mV

//#contract packet_sys_power_monitor_get_current_t
//  @alias         Read shunt current
//  @param channel @arg CHANNEL
//  @returns       current_mA

//#contract packet_sys_power_monitor_set_alert_t
//  @alias         Configure current alert
//  @description   Set the selected channel's critical or warning over-current alert. It is raised as a
//                 power event; link it with an event subscription. On a reversed shunt (the board's own
//                 INA3221) each alert watches one channel: the critical and the warning alert can each
//                 be set on one channel.
//  @param channel @arg CHANNEL
//  @param alert   @arg ALERT-SEVERITY
//  @param threshold_mA

#define HEADER_packet_sys_device_install_ina3221_t 0x44
typedef struct __packed {
  uint8_t device_id;       //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;
  uint8_t i2c_addr;        //@alias I2C Address @one-of [0x40..0x43] @note The A0 pin strap (GND/Vs+/SDA/SCL) selects the address.
  pin_ref_wire_t crit_pin; //@modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP] @note Active-low.
  pin_ref_wire_t warn_pin; //@modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP] @note Active-low.
} packet_sys_device_install_ina3221_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_install_ina3221_t(packet_sys_device_install_ina3221_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing ina3221 (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_ina3221_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr,
                          .crit_pin = pin_ref_from_wire(packet->crit_pin), .warn_pin = pin_ref_from_wire(packet->warn_pin)};
  err_h err = PIN_REFS_BELOW(cfg.device_id, cfg.crit_pin, cfg.warn_pin);
  if (err) return err;
  return d_ina3221_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_INA3221_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_ina3221_t, packet_sys_device_install_ina3221_t, decoder_packet_sys_device_install_ina3221_t)
