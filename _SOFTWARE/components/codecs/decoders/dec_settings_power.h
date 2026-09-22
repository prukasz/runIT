#pragma once
/**
 * @file dec_settings_power.h
 * @brief Persisted power settings packets (stored in sys_settings / NVS).
 *
 * Wire format: [class] [packet] [payload].
 */

#include <sdkconfig.h>
#include "sys_error.h"
#include "sys_interface.h"
#include "sys_power.h"

//@settings power @title Power settings @description Describe the external supply and the battery, so the power budget and battery charge are right. Stored on the board.
//@settings-class SETTINGS_POWER

#undef OWNER
#define OWNER OWNER_DEC_SETTINGS_POWER

#define HEADER_packet_settings_power_set_psu_t 0x01
typedef struct __packed {
  uint32_t max_mV; //@required @alias Supply Voltage @unit mV @note 0 with 0 mA clears it
  uint32_t max_mA; //@required @alias Supply Current Limit @unit mA
} packet_settings_power_set_psu_t;

#define HEADER_packet_settings_power_set_battery_t 0x02
typedef struct __packed {
  uint8_t chemistry;    //@required @alias Chemistry @enum-ref sys_power_battery_e @one-of [$SYS_POWER_BATTERY_NONE, $SYS_POWER_BATTERY_LIPO]
  uint8_t cells;        //@required @alias Cells in Series @min 1
  uint8_t low_pct;      //@required @alias Low Threshold @unit % @min 2 @max 100
  uint8_t critical_pct; //@required @alias Critical Threshold @unit % @min 1 @max 99
} packet_settings_power_set_battery_t;

static inline SE_MUST_USE err_h dec_settings_power_decode(const uint8_t* data, size_t len) {
  if (len == 0) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }
  switch (data[0]) {
    case HEADER_packet_settings_power_set_psu_t: {
      packet_settings_power_set_psu_t packet;
      SE_TRY(convert_to_packet(data + 1, len - 1, &packet, sizeof(packet)));
      return sys_power_set_psu(packet.max_mV, packet.max_mA);
    }
    case HEADER_packet_settings_power_set_battery_t: {
      packet_settings_power_set_battery_t packet;
      SE_TRY(convert_to_packet(data + 1, len - 1, &packet, sizeof(packet)));
      return sys_power_set_battery((sys_power_battery_e)packet.chemistry, packet.cells, packet.low_pct, packet.critical_pct);
    }
    default:
      SE_FAIL(ERR_INTERFACE_UNKNOWN_PACKET, .class_header = CONFIG_RX_PACKET_CLASS_SETTINGS_POWER, .packet_header = data[0]);
  }
}
