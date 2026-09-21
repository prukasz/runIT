#pragma once
/**
 * @file dec_features.h
 * @brief Header-only decoder table for the "Features" packet class (0x05).
 *
 * Wire format handled by this class:
 * @code
 *   [0x05] [0xYY] [ packed payload struct ]
 *    class  packet         sizeof(packet_<name>_t)
 * @endcode
 */

#include <stdint.h>
#include <sys/cdefs.h>
#include "features.h"
#include "sys_error.h"
#include "sys_interface.h"

#define SYS_FEATURES_CLASS_HEADER 0x05
#define DEC_FEATURES_TAG "dec_features"

#undef OWNER
#define OWNER OWNER_SYS_ERRORS_BASE

// ==========================================================================
// Registry & Common Feature Lifecycle (0x01 - 0x0F)
// ==========================================================================

#define HEADER_packet_feature_remove_t 0x01
typedef struct __packed {
  uint8_t feature_id;
} packet_feature_remove_t;

static inline err_h decoder_packet_feature_remove_t(packet_feature_remove_t* packet) {
  ESP_LOGI(DEC_FEATURES_TAG, "removing feature %u", packet->feature_id);
  return feature_remove(packet->feature_id);
}

#define HEADER_packet_feature_remove_all_t 0x02
typedef struct __packed {
  uint8_t dummy;
} packet_feature_remove_all_t;

static inline err_h decoder_packet_feature_remove_all_t(packet_feature_remove_all_t* packet) {
  (void)packet;
  ESP_LOGI(DEC_FEATURES_TAG, "removing all features");
  return feature_remove_all();
}

// ==========================================================================
// Servo Feature Packets (0x10 - 0x1F)
// ==========================================================================

#define HEADER_packet_feature_servo_create_t 0x10
typedef struct __packed {
  uint8_t feature_id;
  uint8_t device_id;
  uint8_t pin_num;
  uint16_t frequency_hz;
  uint16_t pulse_min_us;
  uint16_t pulse_max_us;
  float angle_min;
  float angle_max;
  float default_angle;
  float trim_angle;
  uint8_t inverted;
} packet_feature_servo_create_t;

static inline err_h decoder_packet_feature_servo_create_t(packet_feature_servo_create_t* packet) {
  feature_servo_t cfg = {
      .device_id = packet->device_id,
      .pin_num = packet->pin_num,
      .frequency_hz = packet->frequency_hz,
      .pulse_min_us = packet->pulse_min_us,
      .pulse_max_us = packet->pulse_max_us,
      .angle_min = packet->angle_min,
      .angle_max = packet->angle_max,
      .default_angle = packet->default_angle,
      .trim_angle = packet->trim_angle,
      .inverted = (bool)packet->inverted,
  };
  ESP_LOGI(DEC_FEATURES_TAG, "creating servo %u (dev %u, pin %u)", packet->feature_id, packet->device_id, packet->pin_num);
  return feature_servo_create(packet->feature_id, &cfg);
}

#define HEADER_packet_feature_servo_set_angle_t 0x11
typedef struct __packed {
  uint8_t feature_id;
  float angle;
} packet_feature_servo_set_angle_t;

static inline err_h decoder_packet_feature_servo_set_angle_t(packet_feature_servo_set_angle_t* packet) {
  return feature_servo_set_angle(packet->feature_id, packet->angle);
}

#define HEADER_packet_feature_servo_go_home_t 0x12
typedef struct __packed {
  uint8_t feature_id;
} packet_feature_servo_go_home_t;

static inline err_h decoder_packet_feature_servo_go_home_t(packet_feature_servo_go_home_t* packet) {
  return feature_servo_go_home(packet->feature_id);
}

#define HEADER_packet_feature_servo_set_trim_t 0x13
typedef struct __packed {
  uint8_t feature_id;
  float trim_angle;
} packet_feature_servo_set_trim_t;

static inline err_h decoder_packet_feature_servo_set_trim_t(packet_feature_servo_set_trim_t* packet) {
  return feature_servo_set_trim(packet->feature_id, packet->trim_angle);
}

#define HEADER_packet_feature_servo_attach_t 0x14
typedef struct __packed {
  uint8_t feature_id;
  uint8_t attach;
} packet_feature_servo_attach_t;

static inline err_h decoder_packet_feature_servo_attach_t(packet_feature_servo_attach_t* packet) {
  return packet->attach ? feature_servo_attach(packet->feature_id) : feature_servo_detach(packet->feature_id);
}

// ==========================================================================
// H-Bridge / Motor Feature Packets (0x20 - 0x2F)
// ==========================================================================

#define HEADER_packet_feature_hbridge_create_t 0x20
typedef struct __packed {
  uint8_t feature_id;
  uint8_t bridge_device_id;
  uint8_t channel;
  uint8_t inverted;
  uint16_t fault_route_mask;
  uint8_t fault_static_action_id;
  uint8_t fault_dynamic_action_id;
} packet_feature_hbridge_create_t;

static inline err_h decoder_packet_feature_hbridge_create_t(packet_feature_hbridge_create_t* packet) {
  feature_hbridge_t cfg = {
      .bridge_device_id = packet->bridge_device_id,
      .channel = packet->channel,
      .inverted = (bool)packet->inverted,
      .fault_route_mask = packet->fault_route_mask,
      .fault_static_action_id = packet->fault_static_action_id,
      .fault_dynamic_action_id = packet->fault_dynamic_action_id,
  };
  ESP_LOGI(DEC_FEATURES_TAG, "creating hbridge %u (dev %u, ch %u)", packet->feature_id, packet->bridge_device_id, packet->channel);
  return feature_hbridge_create(packet->feature_id, &cfg);
}

#define HEADER_packet_feature_hbridge_set_speed_t 0x21
typedef struct __packed {
  uint8_t feature_id;
  float speed;
} packet_feature_hbridge_set_speed_t;

static inline err_h decoder_packet_feature_hbridge_set_speed_t(packet_feature_hbridge_set_speed_t* packet) {
  return feature_hbridge_set_speed(packet->feature_id, packet->speed);
}

#define HEADER_packet_feature_hbridge_brake_t 0x22
typedef struct __packed {
  uint8_t feature_id;
} packet_feature_hbridge_brake_t;

static inline err_h decoder_packet_feature_hbridge_brake_t(packet_feature_hbridge_brake_t* packet) {
  return feature_hbridge_brake(packet->feature_id);
}

#define HEADER_packet_feature_hbridge_coast_t 0x23
typedef struct __packed {
  uint8_t feature_id;
} packet_feature_hbridge_coast_t;

static inline err_h decoder_packet_feature_hbridge_coast_t(packet_feature_hbridge_coast_t* packet) {
  return feature_hbridge_coast(packet->feature_id);
}

#define HEADER_packet_feature_hbridge_set_current_limit_t 0x24
typedef struct __packed {
  uint8_t feature_id;
  uint32_t limit_ma;
} packet_feature_hbridge_set_current_limit_t;

static inline err_h decoder_packet_feature_hbridge_set_current_limit_t(packet_feature_hbridge_set_current_limit_t* packet) {
  return feature_hbridge_set_current_limit_ma(packet->feature_id, packet->limit_ma);
}

#define HEADER_packet_feature_hbridge_clear_fault_t 0x25
typedef struct __packed {
  uint8_t feature_id;
} packet_feature_hbridge_clear_fault_t;

static inline err_h decoder_packet_feature_hbridge_clear_fault_t(packet_feature_hbridge_clear_fault_t* packet) {
  return feature_hbridge_clear_fault(packet->feature_id);
}

// ==========================================================================
// Central Packet Dispatcher
// ==========================================================================

#define SYS_FEATURES_PACKET_LIST(X)                                                                               \
  X(HEADER_packet_feature_remove_t, packet_feature_remove_t, decoder_packet_feature_remove_t)                     \
  X(HEADER_packet_feature_remove_all_t, packet_feature_remove_all_t, decoder_packet_feature_remove_all_t)         \
  X(HEADER_packet_feature_servo_create_t, packet_feature_servo_create_t, decoder_packet_feature_servo_create_t)   \
  X(HEADER_packet_feature_servo_set_angle_t, packet_feature_servo_set_angle_t, decoder_packet_feature_servo_set_angle_t) \
  X(HEADER_packet_feature_servo_go_home_t, packet_feature_servo_go_home_t, decoder_packet_feature_servo_go_home_t) \
  X(HEADER_packet_feature_servo_set_trim_t, packet_feature_servo_set_trim_t, decoder_packet_feature_servo_set_trim_t) \
  X(HEADER_packet_feature_servo_attach_t, packet_feature_servo_attach_t, decoder_packet_feature_servo_attach_t) \
  X(HEADER_packet_feature_hbridge_create_t, packet_feature_hbridge_create_t, decoder_packet_feature_hbridge_create_t) \
  X(HEADER_packet_feature_hbridge_set_speed_t, packet_feature_hbridge_set_speed_t, decoder_packet_feature_hbridge_set_speed_t) \
  X(HEADER_packet_feature_hbridge_brake_t, packet_feature_hbridge_brake_t, decoder_packet_feature_hbridge_brake_t) \
  X(HEADER_packet_feature_hbridge_coast_t, packet_feature_hbridge_coast_t, decoder_packet_feature_hbridge_coast_t) \
  X(HEADER_packet_feature_hbridge_set_current_limit_t, packet_feature_hbridge_set_current_limit_t, decoder_packet_feature_hbridge_set_current_limit_t) \
  X(HEADER_packet_feature_hbridge_clear_fault_t, packet_feature_hbridge_clear_fault_t, decoder_packet_feature_hbridge_clear_fault_t)

static inline err_h dec_features_decode(const uint8_t* data, size_t len) {
  if (len == 0) {
    SE_RET_ERR(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }

  const uint8_t packet_header = data[0];

  switch (packet_header) {
#define SYS_FEATURES_DECODE_CASE(header_val, struct_type, decoder_func) \
  case header_val: {                                                    \
    struct_type packet;                                                 \
    SE_RET_IF_ERR(convert_to_packet(data + 1, len - 1, &packet, sizeof(struct_type))); \
    return decoder_func(&packet);                                       \
  }
    SYS_FEATURES_PACKET_LIST(SYS_FEATURES_DECODE_CASE)
#undef SYS_FEATURES_DECODE_CASE

    default:
      ESP_LOGW(DEC_FEATURES_TAG, "unknown features packet byte 0x%02X", packet_header);
      SE_RET_ERR(ERR_INTERFACE_UNKNOWN_PACKET, .class_header = SYS_FEATURES_CLASS_HEADER, .packet_header = packet_header);
  }
}

