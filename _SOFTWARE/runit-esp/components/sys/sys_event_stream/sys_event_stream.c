#include "sys_event_stream.h"

#include <string.h>

#include "cmd.h"
#include "sys_event.h"

static void events_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* data,
                      uint32_t len);

static data_matrix_port_t s_events = {.id = DATA_MATRIX_PORT(DATA_MATRIX_PORT_MODULE, MODULE_ID_SYS_EVENT),
                                      .in = events_in};

// The events that matter to the user, out as they are, [hdr PUSH urgent, MODULE_ID_SYS_EVENT][sys_event_t]: the port
// is told nothing of the event's module. NOWAIT: an event never waits for a line.
static err_h on_notify_event(const sys_event_t* event, void* ctx) {
  (void)ctx;
  const uint32_t len = (uint32_t)(sizeof(*event) + event->len);
  if ((event->flags & SYS_EVENT_NOTIFY) == 0u || len > CMD_DATA_MAX) {
    return NULL;
  }
  data_matrix_push(&s_events, CMD_SEND_URGENT, NULL, 0, event, len);
  return NULL;
}

// A sys_event_t from another board: only passed on, to every port linked from EVENTS but the one it came from (the
// app's session). Never published here: its source and channel are that board's own numbers, which mean nothing to
// this board's listeners; a remote device's events reach them by sys_device_remote, as the local alias's.
static void events_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* data,
                      uint32_t len) {
  sys_event_t event;
  if (len < CMD_HEAD + sizeof(event)) {
    return;
  }
  memcpy(&event, data + CMD_HEAD, sizeof(event));
  if (len != CMD_HEAD + sizeof(event) + event.len) {
    return;
  }
  data_matrix_send_except(self, from, flags, data, len);
}

// Before app_main: the port, and the listener that sends the notify events out of it.
__attribute__((constructor)) static void stream_start(void) {
  err_boot(data_matrix_register(&s_events));
  uint16_t handle = 0;
  err_boot(sys_event_subscribe(
      (sys_event_key_t){.tag = SYS_EVENT_ANY_NOTIFY, .source = SYS_EVENT_ANY_SOURCE, .channel = SYS_EVENT_ANY_CHANNEL},
      on_notify_event, NULL, SYS_EVENT_RUN_FIRST, &handle));
}
