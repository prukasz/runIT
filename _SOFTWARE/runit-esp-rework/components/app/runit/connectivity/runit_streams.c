#include "data_matrix.h"
#include "runit_connectivity.h"
#include "sys_event.h"
#include "sys_log.h"

DATA_MATRIX_STREAMS_DEFINE(RUNIT)

/* The events that matter to the user (SYS_EVENT_NOTIFY, from the events table) go to the app on EVENTS, as the
   sys_event_t they are. */
static err_h on_notify_event(const sys_event_t* event, void* ctx) {
  (void)ctx;
  if (event != NULL && (event->flags & SYS_EVENT_NOTIFY) != 0u) {
    err_free(data_matrix_send(RUNIT_STREAM_EVENTS, event, (uint32_t)(sizeof(*event) + event->len)));
  }
  return NULL;
}

err_h runit_streams_init(void) {
  ERR_TRY(data_matrix_init());
  ERR_TRY(data_matrix_streams(RUNIT_streams, RUNIT_STREAMS_COUNT));
  ERR_TRY(sys_log_init());
  // First in the table (SYS_EVENT_RUN_FIRST): an action that stops the board can't hide its cause.
  uint16_t handle = 0;
  return sys_event_subscribe(
      (sys_event_key_t){.tag = SYS_EVENT_ANY_NOTIFY, .source = SYS_EVENT_ANY_SOURCE, .channel = SYS_EVENT_ANY_CHANNEL},
      on_notify_event, NULL, SYS_EVENT_RUN_FIRST, &handle);
}
