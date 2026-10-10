#pragma once
#include "sys_device.h"
#include "sys_error.h"
#include "sys_io.h"

#define POWER_MUX_TYPE_ID 0x4A  /* the byte after 0x00 in a create frame: [0x00][0x4A][d_power_mux_cfg_t] */

// The annotations below describe the device to the app (data-structures/devices/*.generated.json,
// grammar: data-structures/auto-annotations/device/device-annotations.md).

//#device device_power_mux
//  @title       Power mux (ideal diodes)
//  @description Two switched supply sources into one load, for example the two LM73100 ideal diodes that feed an
//               H-bridge. At most one source is on; changing source switches the old one off first. The choice
//               is a default setting, so the stored hardware config applies it at boot.
//  @protocol    pwm
//  @tags        power mux supply ideal diode switch
//  @datasheet   https://www.ti.com/lit/ds/symlink/lm73100.pdf
//  @type-id     POWER_MUX_TYPE_ID
//  @contract-provider $SYS_DEVICE_CONTRACT_POWER_SUPPLY

//#contract packet_sys_power_supply_select_t
//  @alias       Select source
//  @description Feed the load from source A, source B, or from none. The source that is on is switched off
//               first and the new one is switched on after the switch delay, so the two are never on together.
//  @param source

//#contract packet_sys_power_supply_get_source_t
//  @alias       Read source
//  @returns     source

/**
 * @brief Power mux configuration. Also the wire struct of the create frame (packed, device_id first).
 *
 * The part has no bus: each source switch is enabled by a pin of another device. Both switches are off after
 * install; the source is chosen with the select contract (a default setting, run after the installs).
 *
 * @warning A pin that is not wired MUST be spelled `SYS_IO_PIN_NONE_INIT`; omitting the field
 *          zero-fills it to device 0 / pin 0, which is a real pin.
 */
typedef struct __packed {
  uint8_t device_id;          //@max CONFIG_SYS_DEVICE_MAX_ID
  sys_io_pin_ref_t en_a_pin;  //@alias Enable source A @modes [$SYS_IO_MODE_OUTPUT_PUSH_PULL] @default-mode $SYS_IO_MODE_OUTPUT_PUSH_PULL
                              //  @note Switches source A on when it is at the enable level.
  sys_io_pin_ref_t en_b_pin;  //@alias Enable source B @modes [$SYS_IO_MODE_OUTPUT_PUSH_PULL] @default-mode $SYS_IO_MODE_OUTPUT_PUSH_PULL
                              //  @note Switches source B on when it is at the enable level.
  uint8_t active_level;       //@alias Enable Level @min 0 @max 1 @default 1 @note 1: a high pin switches the source on (LM73100).
  uint8_t switch_delay_ms;    //@alias Switch Delay @unit ms @min 1 @max 100 @default 5 @note Pause between switching the old source off and the new one on.
} d_power_mux_cfg_t;

/** The power mux device class: register with sys_device_register_class(), create with SYS_DEVICE_CREATE(&g_power_mux_class, &cfg). */
extern const sys_device_class_t g_power_mux_class;
