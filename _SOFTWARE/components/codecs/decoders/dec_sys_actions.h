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
 * Packets:
 *   0x03 0x00 YY -> Invoke static action YY
 *   0x03 0x01 ZZ -> Invoke dynamic action ZZ
 *   0x03 0x02 YY -> Record start for dynamic action YY
 *   0x03 0x03    -> Record stop (remembers action id)
 *   0x03 0x04 YY -> Remove dynamic action YY
 *   0x03 0x05    -> Remove all dynamic actions
 */

#include <stdint.h>
#include "sys_actions.h"
#include "sys_error.h"
#include "sys_interface.h"

#undef OWNER
#define OWNER OWNER_DEC_SYS_ACTIONS

/** @brief ESP log tag used by every decoder in this table. */
#define DEC_SYS_ACTIONS_TAG "dec_sys_actions"

#define HEADER_packet_sys_action_static_t 0x00
typedef struct __packed {
  uint8_t id; //@required @alias Static Action ID
} packet_sys_action_static_t;

static inline err_h decoder_packet_sys_action_static_t(packet_sys_action_static_t* packet) {
  ESP_LOGI(DEC_SYS_ACTIONS_TAG, "invoking static action %u", packet->id);
  return sys_actions_invoke(SYS_ACTION_SCOPE_STATIC, packet->id);
}

#define HEADER_packet_sys_action_dynamic_t 0x01
typedef struct __packed {
  uint8_t id; //@required @alias Dynamic Action ID
} packet_sys_action_dynamic_t;

static inline err_h decoder_packet_sys_action_dynamic_t(packet_sys_action_dynamic_t* packet) {
  ESP_LOGI(DEC_SYS_ACTIONS_TAG, "invoking dynamic action %u", packet->id);
  return sys_actions_invoke(SYS_ACTION_SCOPE_DYNAMIC, packet->id);
}

#define HEADER_packet_sys_action_record_start_t 0x02
typedef struct __packed {
  uint8_t id; //@required @alias Dynamic Action ID @note action id that recorded blocks will be attached to
} packet_sys_action_record_start_t;

static inline err_h decoder_packet_sys_action_record_start_t(packet_sys_action_record_start_t* packet) {
  ESP_LOGI(DEC_SYS_ACTIONS_TAG, "recording dynamic action %u", packet->id);
  return sys_action_record_start(packet->id);
}

#define HEADER_packet_sys_action_record_stop_t 0x03
typedef struct __packed {
} packet_sys_action_record_stop_t;

static inline err_h decoder_packet_sys_action_record_stop_t(packet_sys_action_record_stop_t* packet) {
  (void)packet;
  ESP_LOGI(DEC_SYS_ACTIONS_TAG, "stopping recording");
  return sys_action_record_stop();
}

#define HEADER_packet_sys_action_remove_t 0x04
typedef struct __packed {
  uint8_t id; //@required @alias Dynamic Action ID
} packet_sys_action_remove_t;

static inline err_h decoder_packet_sys_action_remove_t(packet_sys_action_remove_t* packet) {
  ESP_LOGI(DEC_SYS_ACTIONS_TAG, "removing dynamic action %u", packet->id);
  return sys_action_remove(packet->id);
}

#define HEADER_packet_sys_action_remove_all_t 0x05
typedef struct __packed {
} packet_sys_action_remove_all_t;

static inline err_h decoder_packet_sys_action_remove_all_t(packet_sys_action_remove_all_t* packet) {
  (void)packet;
  ESP_LOGI(DEC_SYS_ACTIONS_TAG, "removing all dynamic actions");
  return sys_action_remove_all();
}

#define SYS_ACTIONS_PACKET_LIST(X)                                                                                        \
  X(HEADER_packet_sys_action_static_t, packet_sys_action_static_t, decoder_packet_sys_action_static_t)                   \
  X(HEADER_packet_sys_action_dynamic_t, packet_sys_action_dynamic_t, decoder_packet_sys_action_dynamic_t)                \
  X(HEADER_packet_sys_action_record_start_t, packet_sys_action_record_start_t, decoder_packet_sys_action_record_start_t) \
  X(HEADER_packet_sys_action_record_stop_t, packet_sys_action_record_stop_t, decoder_packet_sys_action_record_stop_t)     \
  X(HEADER_packet_sys_action_remove_t, packet_sys_action_remove_t, decoder_packet_sys_action_remove_t)                   \
  X(HEADER_packet_sys_action_remove_all_t, packet_sys_action_remove_all_t, decoder_packet_sys_action_remove_all_t)

#define SYS_ACTIONS_DECODE_CASE(header, packet_type, decoder_func)                     \
  case header: {                                                                       \
    packet_type packet;                                                                \
    SE_RET_IF_ERR(convert_to_packet(data + 1, len - 1, &packet, sizeof(packet_type))); \
    return decoder_func(&packet);                                                      \
  }

/**
 * @brief Class handler for SYS_ACTIONS_CLASS_HEADER (0x03).
 *
 * @param data Frame bytes with the class byte already stripped - data[0] is 0xYY.
 * @param len Number of bytes available at @p data.
 * @return err_h NULL on success, ERR_INTERFACE_UNKNOWN_PACKET for an unmapped
 *               header, or the decoder's own error chain.
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
