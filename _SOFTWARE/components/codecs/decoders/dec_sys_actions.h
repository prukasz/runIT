#pragma once
/**
 * @file dec_sys_actions.h
 * @brief Header-only decoder table for the "System Actions" packet class (0x03).
 *
 * Wire format handled by this class:
 * @code
 *   [0x03] [0xYY] [ packed payload struct ]
 *    class  packet         sizeof(packet_<name>_t)
 * @endcode
 *
 * The outer class byte (0x03, SYS_ACTIONS_CLASS_HEADER) is consumed by
 * sys_interface_decode(), which then hands the remaining bytes to
 * dec_sys_actions_decode() with data[0] == 0xYY.
 *
 * This table only carries the record/stop/remove control packets - the
 * decoders are thin wrappers straight into sys_actions.h, same shape as
 * dec_sys_contracts.h wrapping sys_device/sys_io/sys_power.
 */

#include <stdint.h>
#include <sys/cdefs.h>
#include "sys_actions.h"
#include "sys_error.h"
#include "sys_interface.h"

#undef OWNER
#define OWNER OWNER_DEC_SYS_ACTIONS

/** @brief ESP log tag used by every decoder in this table. */
#define DEC_SYS_ACTIONS_TAG "dec_sys_actions"

#define HEADER_packet_sys_actions_record_t 0x01
typedef struct __packed {
  uint8_t action_id;
} packet_sys_actions_record_t;

static inline err_h decoder_packet_sys_actions_record_t(packet_sys_actions_record_t* packet) {
  ESP_LOGI(DEC_SYS_ACTIONS_TAG, "recording action %u", packet->action_id);
  return sys_actions_record_start(packet->action_id);
}

#define HEADER_packet_sys_actions_stop_t 0x02
typedef struct __packed {
  uint8_t action_id;
} packet_sys_actions_stop_t;

static inline err_h decoder_packet_sys_actions_stop_t(packet_sys_actions_stop_t* packet) {
  ESP_LOGI(DEC_SYS_ACTIONS_TAG, "stopping recording of action %u", packet->action_id);
  return sys_actions_record_stop(packet->action_id);
}

#define HEADER_packet_sys_actions_remove_t 0x03
typedef struct __packed {
  uint8_t action_id;
} packet_sys_actions_remove_t;

static inline err_h decoder_packet_sys_actions_remove_t(packet_sys_actions_remove_t* packet) {
  ESP_LOGI(DEC_SYS_ACTIONS_TAG, "removing action %u", packet->action_id);
  return sys_actions_remove(packet->action_id);
}

#define SYS_ACTIONS_PACKET_LIST(X)                                                                         \
  X(HEADER_packet_sys_actions_record_t, packet_sys_actions_record_t, decoder_packet_sys_actions_record_t)   \
  X(HEADER_packet_sys_actions_stop_t, packet_sys_actions_stop_t, decoder_packet_sys_actions_stop_t)         \
  X(HEADER_packet_sys_actions_remove_t, packet_sys_actions_remove_t, decoder_packet_sys_actions_remove_t)

#define SYS_ACTIONS_DECODE_CASE(header, packet_type, decoder_func)                  \
  case header: {                                                                   \
    packet_type packet;                                                            \
    SE_RET_IF_ERR(convert_to_packet(data + 1, len - 1, &packet, sizeof(packet_type))); \
    return decoder_func(&packet);                                                  \
  }

/**
 * @brief Class handler for SYS_ACTIONS_CLASS_HEADER (0x03).
 *
 * @param data Frame bytes with the class byte already stripped - data[0] is 0xYY.
 * @param len Number of bytes available at @p data.
 * @return err_h NULL on success, ERR_INTERFACE_UNKNOWN_PACKET for an unmapped
 *               header, or the decoder's own error chain.
 *
 * Example:
 *   Start recording action 5: `03 01 05`
 */
static inline err_h dec_sys_actions_decode(const uint8_t* data, size_t len) {
  if (len == 0) {
    SE_RET_ERR(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }

  switch (data[0]) {
    SYS_ACTIONS_PACKET_LIST(SYS_ACTIONS_DECODE_CASE)
    default:
      ESP_LOGW(DEC_SYS_ACTIONS_TAG, "unknown packet header 0x%02X", data[0]);
      SE_RET_ERR(ERR_INTERFACE_UNKNOWN_PACKET, .class_header = SYS_ACTIONS_CLASS_HEADER, .packet_header = data[0]);
  }
}
