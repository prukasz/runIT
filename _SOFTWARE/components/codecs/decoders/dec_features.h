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

#include <sdkconfig.h>
#include "features.h"
#include "sys_error.h"
#include "sys_interface.h"

#define DEC_FEATURES_TAG "dec_features"

#undef OWNER
#define OWNER OWNER_DEC_FEATURES

// ==========================================================================
// Registry & Common Feature Lifecycle (0x01 - 0x0F)
// ==========================================================================

#define HEADER_packet_feature_remove_t 0x01
typedef struct __packed {
  uint8_t feature_id; //@required @alias Feature ID
} packet_feature_remove_t;

static inline SE_MUST_USE err_h decoder_packet_feature_remove_t(packet_feature_remove_t* packet) {
  ESP_LOGI(DEC_FEATURES_TAG, "removing feature %u", packet->feature_id);
  return feature_remove(packet->feature_id);
}

#define HEADER_packet_feature_remove_all_t 0x02
typedef struct __packed {
} packet_feature_remove_all_t;

static inline SE_MUST_USE err_h decoder_packet_feature_remove_all_t(packet_feature_remove_all_t* packet) {
  (void)packet;
  ESP_LOGI(DEC_FEATURES_TAG, "removing all features");
  return feature_remove_all();
}

// ==========================================================================
// Servo Feature Packets (0x10 - 0x1F)
// ==========================================================================

#define HEADER_packet_feature_servo_create_t 0x10
typedef struct __packed {
  uint8_t feature_id;       //@required @alias Feature ID
  uint8_t device_id;        //@required @alias Device ID
  uint8_t pin_num;          //@required @alias Pin Number
  uint16_t frequency_Hz;    //@required @alias PWM Frequency @unit Hz
  uint16_t pulse_min_us;    //@required @alias Min Pulse Width @unit us
  uint16_t pulse_max_us;    //@required @alias Max Pulse Width @unit us
  float angle_min;          //@required @alias Min Angle @unit deg
  float angle_max;          //@required @alias Max Angle @unit deg
  float default_angle;      //@required @alias Default Angle @unit deg
  float trim_angle;         //@required @alias Trim Angle @unit deg
  uint8_t inverted;         //@required @alias Inverted
} packet_feature_servo_create_t;

static inline SE_MUST_USE err_h decoder_packet_feature_servo_create_t(packet_feature_servo_create_t* packet) {
  feature_servo_t cfg = {
      .device_id = packet->device_id,
      .pin_num = packet->pin_num,
      .frequency_Hz = packet->frequency_Hz,
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
  uint8_t feature_id; //@required @alias Feature ID
  float angle;        //@required @alias Target Angle @unit deg
} packet_feature_servo_set_angle_t;

static inline SE_MUST_USE err_h decoder_packet_feature_servo_set_angle_t(packet_feature_servo_set_angle_t* packet) {
  return feature_servo_set_angle(packet->feature_id, packet->angle);
}

#define HEADER_packet_feature_servo_go_home_t 0x12
typedef struct __packed {
  uint8_t feature_id; //@required @alias Feature ID
} packet_feature_servo_go_home_t;

static inline SE_MUST_USE err_h decoder_packet_feature_servo_go_home_t(packet_feature_servo_go_home_t* packet) {
  return feature_servo_go_home(packet->feature_id);
}

#define HEADER_packet_feature_servo_set_trim_t 0x13
typedef struct __packed {
  uint8_t feature_id; //@required @alias Feature ID
  float trim_angle;   //@required @alias Trim Angle @unit deg
} packet_feature_servo_set_trim_t;

static inline SE_MUST_USE err_h decoder_packet_feature_servo_set_trim_t(packet_feature_servo_set_trim_t* packet) {
  return feature_servo_set_trim(packet->feature_id, packet->trim_angle);
}

#define HEADER_packet_feature_servo_attach_t 0x14
typedef struct __packed {
  uint8_t feature_id; //@required @alias Feature ID
  uint8_t attach;     //@required @alias Attach
} packet_feature_servo_attach_t;

static inline SE_MUST_USE err_h decoder_packet_feature_servo_attach_t(packet_feature_servo_attach_t* packet) {
  return packet->attach ? feature_servo_attach(packet->feature_id) : feature_servo_detach(packet->feature_id);
}

// ==========================================================================
// H-Bridge / Motor Feature Packets (0x20 - 0x2F)
// ==========================================================================

#define HEADER_packet_feature_hbridge_create_t 0x20
typedef struct __packed {
  uint8_t feature_id;              //@required @alias Feature ID
  uint8_t bridge_device_id;        //@required @alias Bridge Device ID
  uint8_t channel;                 //@required @alias Channel
  uint8_t inverted;                //@required @alias Inverted
} packet_feature_hbridge_create_t;

static inline SE_MUST_USE err_h decoder_packet_feature_hbridge_create_t(packet_feature_hbridge_create_t* packet) {
  feature_hbridge_t cfg = {
      .bridge_device_id = packet->bridge_device_id,
      .channel = packet->channel,
      .inverted = (bool)packet->inverted,
  };
  ESP_LOGI(DEC_FEATURES_TAG, "creating hbridge %u (dev %u, ch %u)", packet->feature_id, packet->bridge_device_id, packet->channel);
  return feature_hbridge_create(packet->feature_id, &cfg);
}

#define HEADER_packet_feature_hbridge_set_speed_t 0x21
typedef struct __packed {
  uint8_t feature_id; //@required @alias Feature ID
  float speed;        //@required @alias Speed
} packet_feature_hbridge_set_speed_t;

static inline SE_MUST_USE err_h decoder_packet_feature_hbridge_set_speed_t(packet_feature_hbridge_set_speed_t* packet) {
  return feature_hbridge_set_speed(packet->feature_id, packet->speed);
}

#define HEADER_packet_feature_hbridge_brake_t 0x22
typedef struct __packed {
  uint8_t feature_id; //@required @alias Feature ID
} packet_feature_hbridge_brake_t;

static inline SE_MUST_USE err_h decoder_packet_feature_hbridge_brake_t(packet_feature_hbridge_brake_t* packet) {
  return feature_hbridge_brake(packet->feature_id);
}

#define HEADER_packet_feature_hbridge_coast_t 0x23
typedef struct __packed {
  uint8_t feature_id; //@required @alias Feature ID
} packet_feature_hbridge_coast_t;

static inline SE_MUST_USE err_h decoder_packet_feature_hbridge_coast_t(packet_feature_hbridge_coast_t* packet) {
  return feature_hbridge_coast(packet->feature_id);
}

#define HEADER_packet_feature_hbridge_set_current_limit_t 0x24
typedef struct __packed {
  uint8_t feature_id; //@required @alias Feature ID
  uint32_t limit_mA;  //@required @alias Current Limit @unit mA
} packet_feature_hbridge_set_current_limit_t;

static inline SE_MUST_USE err_h decoder_packet_feature_hbridge_set_current_limit_t(packet_feature_hbridge_set_current_limit_t* packet) {
  return feature_hbridge_set_current_limit_mA(packet->feature_id, packet->limit_mA);
}

#define HEADER_packet_feature_hbridge_clear_fault_t 0x25
typedef struct __packed {
  uint8_t feature_id; //@required @alias Feature ID
} packet_feature_hbridge_clear_fault_t;

static inline SE_MUST_USE err_h decoder_packet_feature_hbridge_clear_fault_t(packet_feature_hbridge_clear_fault_t* packet) {
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

static inline SE_MUST_USE err_h dec_features_decode(const uint8_t* data, size_t len) {
  if (len == 0) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }

  const uint8_t packet_header = data[0];

  switch (packet_header) {
#define SYS_FEATURES_DECODE_CASE(header_val, struct_type, decoder_func) \
  case header_val: {                                                    \
    struct_type packet;                                                 \
    SE_TRY(convert_to_packet(data + 1, len - 1, &packet, sizeof(struct_type))); \
    return decoder_func(&packet);                                       \
  }
    SYS_FEATURES_PACKET_LIST(SYS_FEATURES_DECODE_CASE)
#undef SYS_FEATURES_DECODE_CASE

    default:
      ESP_LOGW(DEC_FEATURES_TAG, "unknown features packet byte 0x%02X", packet_header);
      SE_FAIL(ERR_INTERFACE_UNKNOWN_PACKET, .class_header = CONFIG_RX_PACKET_CLASS_SYS_FEATURES, .packet_header = packet_header);
  }
}
