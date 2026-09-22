#pragma once
/**
 * @file dec_sys_events.h
 * @brief Header-only decoder table for the "System Events" packet class (0x09).
 *
 * Wire format handled by this class:
 * @code
 *   [0x09] [0xYY] [ packed payload struct ]
 *    class  packet         sizeof(packet_<name>_t)
 * @endcode
 *
 * Links an event source to routes and actions: a subscription matches events
 * by domain / device / channel / event (255 = any) and delivers them through
 * the event queue. Packets can't make inline or C-handler listeners.
 */

#include <sdkconfig.h>
#include <stdint.h>
#include "sys_error.h"
#include "sys_event.h"
#include "sys_interface.h"

//@contract-catalog events @title Events @description Link events (pin changes, power alerts, motor faults, connection changes) to the program and to actions.
//@contract-list SYS_EVENTS_PACKET_LIST

#undef OWNER
#define OWNER OWNER_DEC_SYS_EVENTS

#define DEC_SYS_EVENTS_TAG "dec_sys_events"

#define HEADER_packet_sys_event_subscribe_t 0x01
typedef struct __packed {
  uint8_t domain;            //@required @alias Event Source @enum-ref sys_event_domain_e @one-of [$SYS_EVENT_DOMAIN_IO, $SYS_EVENT_DOMAIN_POWER, $SYS_EVENT_DOMAIN_HBRIDGE, $SYS_EVENT_DOMAIN_BLE, $SYS_EVENT_DOMAIN_FEATURE] @note 255 = any source
  uint8_t device_id;         //@required @alias Device ID @note 255 = any device (also board power events)
  uint8_t channel;           //@required @alias Pin or Channel @note 255 = any
  uint8_t event;             //@required @alias Event @note per source: IO edge / window, power event, H-bridge fault, BLE event; 255 = any
  uint8_t route_mask;        //@optional @alias Routes @note bitmask of route slots (bit 0 = program)
  uint8_t static_action_id;  //@optional @alias System Action @sentinel 0
  uint8_t dynamic_action_id; //@optional @alias User Action @sentinel 0
} packet_sys_event_subscribe_t;

typedef struct __packed {
  uint8_t subscription_id; //@alias Subscription ID
} packet_sys_event_subscribe_response_t;

static inline SE_MUST_USE err_h decoder_packet_sys_event_subscribe_t(packet_sys_event_subscribe_t* packet) {
  sys_event_subscription_t sub = {
      .domain = packet->domain,
      .device_id = packet->device_id,
      .channel = packet->channel,
      .event = packet->event,
      .user = true,
      .route_mask = packet->route_mask,
      .static_action_id = packet->static_action_id,
      .dynamic_action_id = packet->dynamic_action_id,
  };
  uint8_t id = 0;
  SE_TRY(sys_event_subscribe(&sub, &id));
  ESP_LOGI(DEC_SYS_EVENTS_TAG, "subscription %u: %u/%u/%u/%u -> routes 0x%02X, actions [%u,%u]", id, packet->domain, packet->device_id,
           packet->channel, packet->event, packet->route_mask, packet->static_action_id, packet->dynamic_action_id);
  packet_sys_event_subscribe_response_t rsp = {.subscription_id = id};
  return sys_interface_respond(&rsp, sizeof(rsp));
}

#define HEADER_packet_sys_event_unsubscribe_t 0x02
typedef struct __packed {
  uint8_t subscription_id; //@required @alias Subscription ID @note 255 = every subscription made by packets
} packet_sys_event_unsubscribe_t;

static inline SE_MUST_USE err_h decoder_packet_sys_event_unsubscribe_t(packet_sys_event_unsubscribe_t* packet) {
  ESP_LOGI(DEC_SYS_EVENTS_TAG, "unsubscribing %u", packet->subscription_id);
  return sys_event_unsubscribe(packet->subscription_id, true);
}

#define SYS_EVENTS_PACKET_LIST(X)                                                                                  \
  X(HEADER_packet_sys_event_subscribe_t, packet_sys_event_subscribe_t, decoder_packet_sys_event_subscribe_t)       \
  X(HEADER_packet_sys_event_unsubscribe_t, packet_sys_event_unsubscribe_t, decoder_packet_sys_event_unsubscribe_t)

#define SYS_EVENTS_DECODE_CASE(header, packet_type, decoder_func)                  \
  case header: {                                                                    \
    packet_type packet;                                                             \
    SE_TRY(convert_to_packet(data + 1, len - 1, &packet, sizeof(packet_type)));     \
    return decoder_func(&packet);                                                   \
  }

/** @brief Class handler for RX_PACKET_CLASS_SYS_EVENTS; data[0] is the packet header. */
static inline SE_MUST_USE err_h dec_sys_events_decode(const uint8_t* data, size_t len) {
  if (len == 0) {
    SE_FAIL(ERR_INTERFACE_SHORT_FRAME, .got = 0, .need = 1);
  }
  switch (data[0]) {
    SYS_EVENTS_PACKET_LIST(SYS_EVENTS_DECODE_CASE)
    default:
      SE_FAIL(ERR_INTERFACE_UNKNOWN_PACKET, .class_header = CONFIG_RX_PACKET_CLASS_SYS_EVENTS, .packet_header = data[0]);
  }
}
