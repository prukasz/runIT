#pragma once
#include "sys_device.h"
#include "sys_error.h"
#include "sys_io.h"

#define AP33772S_TYPE_ID 0x45  /* the byte after 0x00 in a create frame: [0x00][0x45][d_ap33772s_cfg_t] */

// The annotations below describe the device to the app (data-structures/devices/*.generated.json,
// grammar: data-structures/auto-annotations/device/device-annotations.md).

//#device device_ap33772s
//  @title       AP33772S USB-C PD sink
//  @description USB-C Power Delivery sink controller that negotiates voltage/current from a charger and
//               can also act as a regulated, monitored output.
//  @protocol    i2c
//  @tags        i2c usb-c power-delivery power voltage current
//  @datasheet   https://www.diodes.com/assets/Datasheets/AP33772S.pdf
//  @type-id     AP33772S_TYPE_ID
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

/**
 * @brief AP33772S configuration. Also the wire struct of the create frame (packed, device_id first).
 *
 * @warning An interrupt pin that is not wired MUST be spelled `SYS_IO_PIN_NONE_INIT`; omitting the
 *          field zero-fills it to device 0 / pin 0, which is a real pin.
 */
typedef struct __packed {
  uint8_t device_id;       //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;
  uint8_t i2c_addr;        //@alias I2C Address @one-of [0x52] @note Fixed I2C address of the AP33772S.
  sys_io_pin_ref_t intr_pin; //@modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP]
} d_ap33772s_cfg_t;

/** The AP33772S device class: register with sys_device_register_class(), create with SYS_DEVICE_CREATE(&g_ap33772s_class, &cfg). */
extern const sys_device_class_t g_ap33772s_class;
