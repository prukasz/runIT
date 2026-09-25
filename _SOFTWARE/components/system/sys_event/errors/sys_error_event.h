#pragma once
#include <stdint.h>
#include <stdio.h>

// Error owners and tags for events (sys_event_*). sys_errors aggregates these maps
// through its include dirs (sys_error_codes.h). This folder holds only error
// maps, so it doesn't expose the module's API to everything that includes sys_error.h.
#define SYS_EVENT_OWNER_MAP(X)                                          \
  X(OWNER_SYS_EVENT_BASE, 0xAB00, "OWNER_SYS_EVENT_BASE")               \
  X(OWNER_SYS_EVENT_INIT, 0xAB01, "OWNER_SYS_EVENT_INIT")               \
  X(OWNER_SYS_EVENT_PUBLISH, 0xAB02, "OWNER_SYS_EVENT_PUBLISH")         \
  X(OWNER_SYS_EVENT_REGISTER_ROUTE, 0xAB03, "OWNER_SYS_EVENT_REGISTER_ROUTE") \
  X(OWNER_SYS_EVENT_SUBSCRIBE, 0xAB04, "OWNER_SYS_EVENT_SUBSCRIBE")     \
  X(OWNER_SYS_EVENT_DISPATCH, 0xAB05, "OWNER_SYS_EVENT_DISPATCH")

#define SYS_ERROR_EVENT_MAP(X)                                                                                                  \
  X(ERR_EVENT_LOOP, 0xAB01, SE_LEVEL_MEDIUM, struct { uint8_t domain; /*@enum-ref sys_event_domain_e*/ uint8_t device_id; /*@id device*/ uint8_t channel; uint8_t event; })    \
  X(ERR_EVENT_TABLE_FULL, 0xAB02, SE_LEVEL_HIGH, struct { uint8_t capacity; })                                                  \
  X(ERR_EVENT_QUEUE_FULL, 0xAB03, SE_LEVEL_MEDIUM, struct { uint32_t dropped; })                                                \
  X(ERR_EVENT_SUB_NOT_FOUND, 0xAB04, SE_LEVEL_LOW, struct { uint8_t id; })                                                      \
  X(ERR_EVENT_SUB_PROTECTED, 0xAB05, SE_LEVEL_LOW, struct { uint8_t id; })                                                      \
  X(ERR_EVENT_SUB_INVALID, 0xAB06, SE_LEVEL_HIGH, struct { uint8_t inline_call; uint8_t has_handler; uint8_t route_mask; })   \
  X(ERR_EVENT_NO_EXECUTOR, 0xAB07, SE_LEVEL_HIGH, struct { uint8_t action_id; })

/** @brief Human-readable descriptions for the sys_event tags - see SE_describe_payload() in sys_error.h. */
#define SYS_ERROR_EVENT_LOGGER_MAP(X) \
  X(ERR_EVENT_LOOP) X(ERR_EVENT_TABLE_FULL) X(ERR_EVENT_QUEUE_FULL) X(ERR_EVENT_SUB_NOT_FOUND) X(ERR_EVENT_SUB_PROTECTED) X(ERR_EVENT_SUB_INVALID) X(ERR_EVENT_NO_EXECUTOR)

#define LOG_BODY_ERR_EVENT_LOOP(p, out, out_size) snprintf((out), (out_size), "event %u/%u/%u/%u dropped: republished too many times (loop?)", (p)->domain, (p)->device_id, (p)->channel, (p)->event)
#define LOG_BODY_ERR_EVENT_TABLE_FULL(p, out, out_size) snprintf((out), (out_size), "all %u event subscriptions are in use", (p)->capacity)
#define LOG_BODY_ERR_EVENT_QUEUE_FULL(p, out, out_size) snprintf((out), (out_size), "event queue full: %lu event(s) dropped", (unsigned long)(p)->dropped)
#define LOG_BODY_ERR_EVENT_SUB_NOT_FOUND(p, out, out_size) snprintf((out), (out_size), "no event subscription %u", (p)->id)
#define LOG_BODY_ERR_EVENT_SUB_PROTECTED(p, out, out_size) snprintf((out), (out_size), "event subscription %u belongs to the system and can't be removed", (p)->id)
#define LOG_BODY_ERR_EVENT_SUB_INVALID(p, out, out_size) snprintf((out), (out_size), "invalid event subscription (inline %u, handler %u, routes 0x%02X)", (p)->inline_call, (p)->has_handler, (p)->route_mask)
#define LOG_BODY_ERR_EVENT_NO_EXECUTOR(p, out, out_size) snprintf((out), (out_size), "action %u requested by an event, but no action executor is registered", (p)->action_id)
