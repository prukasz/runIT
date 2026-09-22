#pragma once

#include "dec_device_common.h"
#include "../../../devices/device_ap33772s/include/device_ap33772s.h"

//@id device_ap33772s
//@version 1.0.0
//@title AP33772S USB-C PD sink
//@description USB-C Power Delivery sink controller that negotiates voltage/current from a charger and can also act as a regulated, monitored output.
//@protocol i2c
//@tags i2c usb-c power-delivery power voltage current
//@contract-provider $SYS_DEVICE_CONTRACT_POWER_USB_PD
//@self-property CHANNEL @one-of [0]
//@property ALERT-SEVERITY @ref sys_power_events_e @one-of [$SYS_PWR_EVENT_OCP_CRITICAL, $SYS_PWR_EVENT_OCP_WARNING, $SYS_PWR_EVENT_OVP, $SYS_PWR_EVENT_UVP, $SYS_PWR_EVENT_OTP]

//@contract packet_sys_power_usb_pd_set_t @alias Request USB-PD profile
//@param voltage_mV @alias Requested Voltage @type uint32_t @unit mV
//@param current_mA @alias Requested Current @type uint32_t @unit mA
//@description Ask the charger for a specific voltage/current profile. The charger may not grant it exactly - read back the actual result with the negotiated-limits contract.

//@contract packet_sys_power_usb_pd_list_t @alias List available power profiles
//@description Lists the voltage/current profiles this charger is offering.

//@contract packet_sys_power_usb_pd_get_limits_t @alias Read negotiated limits
//@description Reads the voltage/current the charger actually agreed to supply.

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

//@contract packet_sys_power_monitor_get_voltage_t @alias Read output voltage
//@param channel @arg CHANNEL @alias Channel
//@returns voltage_mV @type int32_t @unit mV
//@description Single-rail device - channel is always 0.

//@contract packet_sys_power_monitor_get_current_t @alias Read output current
//@param channel @arg CHANNEL @alias Channel
//@returns current_mA @type int32_t @unit mA
//@description Single-rail device - channel is always 0.

#define HEADER_packet_sys_device_install_ap33772s_t 0x45
typedef struct __packed {
  uint8_t device_id; //@required @min 0 @max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;   //@required @min 0 @max 1
  uint8_t i2c_addr;  //@required @default 0x52 @note AP33772S has a fixed datasheet address (AP33772S_ADDRESS); the field is honored (adapter_ap33772s.c overwrites the driver handle's address with it) but the real chip only answers at 0x52
  uint8_t intr_pin_device_id; //@group interrupt-pin @role device_id
  uint8_t intr_pin_pin;       //@group interrupt-pin @role pin @sentinel SYS_GPIO_NONE
  uint8_t intr_pin_mode;      //@group interrupt-pin @role mode @ref sys_io_mode_e
} packet_sys_device_install_ap33772s_t;

static inline err_h decoder_packet_sys_device_install_ap33772s_t(packet_sys_device_install_ap33772s_t* packet) {
  ESP_LOGI(DEC_SYS_DEVICE_INSTALL_TAG, "installing ap33772s (dev %u, i2c bus %u addr 0x%02X)", packet->device_id, packet->i2c_bus, packet->i2c_addr);
  d_ap33772s_cfg_t cfg = {.device_id = packet->device_id, .i2c_bus = packet->i2c_bus != 0, .i2c_addr = packet->i2c_addr,
                           .intr_pin = pin_ref_from_wire(packet->intr_pin_device_id, packet->intr_pin_pin, packet->intr_pin_mode)};
  return d_ap33772s_create(&cfg);
}

#define SYS_CONTRACTS_DEVICE_AP33772S_PACKET_LIST(X) \
  X(HEADER_packet_sys_device_install_ap33772s_t, packet_sys_device_install_ap33772s_t, decoder_packet_sys_device_install_ap33772s_t)
