#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * @file sys_event_filter.h
 * @brief Draft of what sys_event.h gains: one filter of events for every module that picks events (a watch's ON_EVENT,
 * the VM's ON_EVENT block), in place of the three loose arguments of sys_event_matches. Each field is exact or its
 * wildcard (SYS_EVENT_ANY_SOURCE, SYS_EVENT_ANY_EVENT or (contract << 8) | SYS_EVENT_ANY_IN, SYS_EVENT_ANY_CHANNEL).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Which events: packed, as commands and descriptors carry it (5 B). */
typedef struct __attribute__((packed)) {
  uint16_t source;  /**< Device id, or SYS_EVENT_ANY_SOURCE */
  uint16_t event;   /**< SYS_EVENT_ID, SYS_EVENT_ANY_EVENT or (contract << 8) | SYS_EVENT_ANY_IN */
  uint8_t channel;  /**< Pin / channel, or SYS_EVENT_ANY_CHANNEL */
} sys_event_filter_t;

typedef struct sys_event sys_event_t;  // sys_event.h's

/**
 * @brief Whether an event passes a filter (sys_event_matches on the filter's fields). Any context, an ISR too.
 * @param f  The filter.
 * @param e  The event.
 * @return true when it passes.
 */
bool sys_event_filter_match(const sys_event_filter_t* f, const sys_event_t* e);

#ifdef __cplusplus
}
#endif
