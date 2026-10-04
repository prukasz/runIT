#pragma once
#include "sys_device.h"
#include "sys_error.h"
#include "sys_io.h"

#define INA3221_TYPE_ID 0x44  /* the byte after 0x00 in a create frame: [0x00][0x44][d_ina3221_cfg_t] */

// The annotations below describe the device to the app (data-structures/devices/*.generated.json,
// grammar: data-structures/auto-annotations/device/device-annotations.md).

//#device device_ina3221
//  @title       INA3221 power monitor
//  @description Three-channel I2C bus-voltage and shunt-current monitor with warning and critical alerts.
//  @protocol    i2c
//  @tags        i2c power voltage current monitor alert ic
//  @datasheet   https://www.ti.com/lit/ds/symlink/ina3221.pdf
//  @type-id     INA3221_TYPE_ID
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

/**
 * @brief INA3221 configuration. Also the wire struct of the create frame (packed, device_id first).
 *
 * @warning An alert pin that is not wired MUST be spelled `SYS_IO_PIN_NONE_INIT`; omitting the
 *          field zero-fills it to device 0 / pin 0, which is a real pin.
 */
typedef struct __packed {
  uint8_t device_id;       //@max CONFIG_SYS_DEVICE_MAX_ID
  uint8_t i2c_bus;
  uint8_t i2c_addr;        //@alias I2C Address @one-of [0x40..0x43] @note The A0 pin strap (GND/Vs+/SDA/SCL) selects the address.
  sys_io_pin_ref_t crit_pin; //@modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP] @note Active-low.
  sys_io_pin_ref_t warn_pin; //@modes [$SYS_IO_MODE_INPUT, $SYS_IO_MODE_INPUT_PULLUP] @note Active-low.
  uint8_t inverted_mask;   //@alias Reversed Shunts @min 0 @max 7 @default 0
                           //  @note Bit N set: channel N's shunt is wired reversed. Its current is reported with the sign flipped;
                           //  its alerts are armed on the reversed (released) edge, one channel per alert pin.
} d_ina3221_cfg_t;

/** The INA3221 device class: register with sys_device_register_class(), create with SYS_DEVICE_CREATE(&g_ina3221_class, &cfg). */
extern const sys_device_class_t g_ina3221_class;
