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
  uint8_t packet[CMD_HEAD + len];
  const cmd_hdr_t hdr = {.type = (uint8_t)(CMD_TYPE(CMD_PUSH) | CMD_SEND_URGENT), .key = MODULE_ID_SYS_EVENT};
  memcpy(packet, &hdr, CMD_HEAD);
  memcpy(packet + CMD_HEAD, event, len);
  data_matrix_send(&s_events, CMD_SEND_NOWAIT | CMD_SEND_URGENT, packet, sizeof(packet));
  return NULL;
}

// A sys_event_t from another board: passed on to every port linked from EVENTS but the one it came from, and
// published here without NOTIFY (what it sent out was that board's to tell). Its source and channel are that board's
// own numbers.
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
  err_free(sys_event_publish(event.key, (uint16_t)((event.flags & ~SYS_EVENT_NOTIFY) | SYS_EVENT_DEFER),
                             data + CMD_HEAD + sizeof(event), event.len));
}

err_h sys_event_stream_init(void) {
  ERR_TRY(data_matrix_register(&s_events));
  uint16_t handle = 0;
  return sys_event_subscribe(
      (sys_event_key_t){.tag = SYS_EVENT_ANY_NOTIFY, .source = SYS_EVENT_ANY_SOURCE, .channel = SYS_EVENT_ANY_CHANNEL},
      on_notify_event, NULL, SYS_EVENT_RUN_FIRST, &handle);
}
