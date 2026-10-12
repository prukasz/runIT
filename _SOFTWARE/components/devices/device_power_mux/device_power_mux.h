#pragma once

#include "sys_device.h"
#include "sys_io.h"
#include "sys_power_supply.h"

//#device
//@alias Power mux (ideal diodes)
//@description Two switched supply sources into one load, for example the two LM73100 ideal diodes that feed an H-bridge. At most one source is on; changing source switches the old one off first.
//@protocol pwm
//@tags power mux supply ideal diode switch
//@datasheet https://www.ti.com/lit/ds/symlink/lm73100.pdf

#ifdef __cplusplus
extern "C" {
#endif

#define POWER_MUX_MIN_SWITCH_DELAY_MS 1u
#define POWER_MUX_MAX_SWITCH_DELAY_MS 100u

/**
 * @brief Create struct (packed, also the wire form):
 * CREATE = [0x05][id u16][op 0x0000][type u16 = SYS_DEVICE_TYPE_POWER_MUX][power_mux_cfg_t].
 *
 * The part has no bus: each source switch is enabled by a pin of another device. Both switches are off after
 * create; the source is chosen with the SELECT op (a default setting, run after the creates).
 *
 * @warning Both pins are required and must differ (one pin for both could switch the two sources on together).
 * Their devices must exist (sys_device_use) and can't be removed while this one does.
 */
typedef struct __attribute__((packed)) {
  sys_io_pin_t en_a_pin;  //@alias Enable source A @description Switches source A on when it is at the enable level @modes [$SYS_IO_MODE_OUTPUT_PUSH_PULL, $SYS_IO_MODE_OUTPUT_OPEN_DRAIN] @required @default-mode $SYS_IO_MODE_OUTPUT_PUSH_PULL
  sys_io_pin_t en_b_pin;  //@alias Enable source B @description Switches source B on when it is at the enable level @modes [$SYS_IO_MODE_OUTPUT_PUSH_PULL, $SYS_IO_MODE_OUTPUT_OPEN_DRAIN] @required @default-mode $SYS_IO_MODE_OUTPUT_PUSH_PULL
  uint8_t active_level;  //@alias Enable level @description 1: a high pin switches the source on (the LM73100) @min 0 @max 1 @default 1 @choices 0=Low,1=High
  uint8_t switch_delay_ms;  //@alias Switch delay @description Pause between the old source off and the new one on @unit ms @min POWER_MUX_MIN_SWITCH_DELAY_MS @max POWER_MUX_MAX_SWITCH_DELAY_MS @default 5
} power_mux_cfg_t;

#ifdef __cplusplus
}
#endif
