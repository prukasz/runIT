#include "sys_event_stream.h"

#include <string.h>

#include "sys_event.h"

DATA_MATRIX_STREAMS_DEFINE(EVENT)

// The events that matter to the user, out as they are: the stream is told nothing of the event's module.
static err_h on_notify_event(const sys_event_t* event, void* ctx) {
  (void)ctx;
  if (event != NULL && (event->flags & SYS_EVENT_NOTIFY) != 0u) {
    err_free(data_matrix_send(EVENT_STREAM_NOTIFY, event, (uint32_t)(sizeof(*event) + event->len)));
  }
  return NULL;
}

// A sys_event_t from another board: published here, without NOTIFY (what it sent out was that board's to tell: sent on
// again, two boards would pass it between them for ever). Its source and channel are that board's own numbers.
static void on_event_in(void* ctx, const data_matrix_end_t* from, const uint8_t* data, uint32_t len) {
  (void)ctx;
  (void)from;
  sys_event_t event;
  if (len < sizeof(event)) {
    return;
  }
  memcpy(&event, data, sizeof(event));
  if (len != sizeof(event) + event.len) {
    return;
  }
  err_free(sys_event_publish(event.key, (uint16_t)((event.flags & ~SYS_EVENT_NOTIFY) | SYS_EVENT_DEFER),
                             data + sizeof(event), event.len));
}

err_h sys_event_stream_init(void) {
  ERR_TRY(data_matrix_streams(EVENT_streams, EVENT_STREAMS_COUNT));
  uint16_t handle = 0;
  ERR_TRY(sys_event_subscribe(
      (sys_event_key_t){.tag = SYS_EVENT_ANY_NOTIFY, .source = SYS_EVENT_ANY_SOURCE, .channel = SYS_EVENT_ANY_CHANNEL},
      on_notify_event, NULL, SYS_EVENT_RUN_FIRST, &handle));
  data_matrix_end_t sink;
  ERR_TRY(data_matrix_listen(on_event_in, NULL, &sink));
  return data_matrix_route(&(data_matrix_route_t){DATA_MATRIX_END_STREAM(EVENT_STREAM_REMOTE), sink});
}
