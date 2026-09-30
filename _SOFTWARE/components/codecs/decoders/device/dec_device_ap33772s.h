#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_ap33772s/include/device_ap33772s.h"

//#device device_ap33772s
//  @title       AP33772S USB-C PD sink
//  @description USB-C Power Delivery sink controller that negotiates voltage/current from a charger and
//               can also act as a regulated, monitored output.
//  @protocol    i2c
//  @tags        i2c usb-c power-delivery power voltage current
//  @datasheet   https://www.diodes.com/assets/Datasheets/AP33772S.pdf
//  @contract-provider $SYS_DEVICE_CONTRACT_POWER_USB_PD

//#self-property CHANNEL
//  @one-of   [0]

//#contract packet_sys_power_usb_pd_set_t
//  @alias       Request USB-PD profile
//  @description Ask the charger for a specific voltage/current profile. The charger may not grant it
//               exactly - read back the actual result with the negotiated-limits contract.
//  @param voltage_mV
//  @param current_mA

//#contract packet_sys_power_usb_pd_list_t
//  @alias       List available power profiles
//  @description Lists the voltage/current profiles this charger is offering.

//#contract packet_sys_power_usb_pd_get_limits_t
//  @alias       Read negotiated limits
//  @description Reads the voltage/current the charger actually agreed to supply.

//#contract packet_sys_power_vreg_set_enable_t
//  @alias       Enable output
//  @param state @alias Enabled @type bool

//#contract packet_sys_power_vreg_set_voltage_t
//  @alias       Set output voltage
//  @param voltage_mV

//#contract packet_sys_power_vreg_set_current_t
//  @alias       Set output current limit
//  @param current_mA

//#contract packet_sys_power_monitor_get_voltage_t
//  @alias         Read output voltage
//  @description   Single-rail device - channel is always 0.
//  @param channel @arg CHANNEL
//  @returns       voltage_mV

//#contract packet_sys_power_monitor_get_current_t
//  @alias         Read output current
//  @description   Single-rail device - channel is always 0.
//  @param channel @arg CHANNEL
//  @returns       current_mA

#define HEADER_packet_sys_device_install_ap33772s_t 0x45
typedef struct __packed {
  uint8_t device_id;       //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;
  uint8_t i2c_addr;        //@alias I2C Address @one-of [0x52] @note Fixed I2C address of the AP33772S.
  pin_ref_wire_t intr_pin; //@modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP]
} packet_sys_device_install_ap33772s_t;

static inline SE_MUST_USE err_h decoder_packet_sys_device_install_ap33772s_t(packet_sys_device_install_ap33772s_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing ap33772s (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_ap33772s_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr,
                           .intr_pin = pin_ref_from_wire(packet->intr_pin)};
  err_h err = PIN_REFS_BELOW(cfg.device_id, cfg.intr_pin);
  if (err) return err;
  return d_ap33772s_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_AP33772S_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_ap33772s_t, packet_sys_device_install_ap33772s_t, decoder_packet_sys_device_install_ap33772s_t)
