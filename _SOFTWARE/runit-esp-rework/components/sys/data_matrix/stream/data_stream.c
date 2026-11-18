/**
 * @file data_stream.c
 * @brief The switch of the stream ports (data_stream.h): the registered ports, each with the ports its output goes
 * to, and out() that hands a packet unchanged to their inputs. NOT IN THE BUILD yet (DATA_MATRIX.MD "data_stream").
 *
 * A port's targets are its own (at most CONFIG_DATA_STREAM_TARGETS, as places in the table), so out() only copies
 * them under the spinlock, a few bytes, and calls them after it: a port's pass never runs under the lock.
 */

#include "data_stream.h"

#include <string.h>
#include "data_matrix_errors.h"
#include "freertos/FreeRTOS.h"

#ifndef CONFIG_DATA_STREAM_PORTS
#define CONFIG_DATA_STREAM_PORTS 32
#endif
#ifndef CONFIG_DATA_STREAM_TARGETS
#define CONFIG_DATA_STREAM_TARGETS 4
#endif

_Static_assert(CONFIG_DATA_STREAM_PORTS < 0xFF, "a port's place is a u8, 0xFF is none");

#define NO_PORT 0xFFu

// A registered port and where its output goes: the places of its targets.
typedef struct {
  const data_stream_port_t* port;  // NULL: a free place
  uint8_t to[CONFIG_DATA_STREAM_TARGETS];
  uint8_t count;
} slot_t;

static slot_t s_slots[CONFIG_DATA_STREAM_PORTS];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

// The place of the registered port with this name, NO_PORT when there is none. Under the lock.
static uint8_t find(data_stream_port_id_t id) {
  for (uint8_t i = 0; i < CONFIG_DATA_STREAM_PORTS; i++) {
    if (s_slots[i].port != NULL && s_slots[i].port->id == id) {
      return i;
    }
  }
  return NO_PORT;
}

// The output every port gets: `link` is its place.
static void out(void* link, const uint8_t* data, uint32_t len) {
  const slot_t* from = &s_slots[(uintptr_t)link];
  const data_stream_port_t* to[CONFIG_DATA_STREAM_TARGETS];
  data_stream_port_id_t id = 0;
  uint32_t n = 0;
  portENTER_CRITICAL(&s_lock);
  if (from->port != NULL) {
    id = from->port->id;
    for (n = 0; n < from->count; n++) {
      to[n] = s_slots[from->to[n]].port;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < n; i++) {
    to[i]->pass(to[i]->ctx, id, data, len);
  }
}

err_h data_stream_register(const data_stream_port_t* port) {
  ERR_CHECK(port != NULL && (port->pass != NULL || port->attach != NULL), ERR_DATA_MATRIX_BAD_ARG);
  uint8_t at = NO_PORT;
  portENTER_CRITICAL(&s_lock);
  const bool taken = find(port->id) != NO_PORT;
  for (uint8_t i = 0; !taken && at == NO_PORT && i < CONFIG_DATA_STREAM_PORTS; i++) {
    if (s_slots[i].port == NULL) {
      s_slots[i] = (slot_t){.port = port};
      at = i;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(!taken, ERR_DATA_MATRIX_BAD_ARG);
  ERR_CHECK(at != NO_PORT, ERR_DATA_MATRIX_FULL);
  if (port->attach != NULL) {
    port->attach(port->ctx, out, (void*)(uintptr_t)at);
  }
  return NULL;
}

err_h data_stream_unregister(data_stream_port_id_t id) {
  const data_stream_port_t* port = NULL;
  portENTER_CRITICAL(&s_lock);
  const uint8_t at = find(id);
  if (at != NO_PORT) {
    port = s_slots[at].port;
    s_slots[at] = (slot_t){0};
    for (uint8_t i = 0; i < CONFIG_DATA_STREAM_PORTS; i++) {  // the links to it go with it
      slot_t* s = &s_slots[i];
      uint8_t kept = 0;
      for (uint8_t k = 0; k < s->count; k++) {
        if (s->to[k] != at) {
          s->to[kept++] = s->to[k];
        }
      }
      s->count = kept;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(port != NULL, ERR_DATA_MATRIX_BAD_ARG);
  if (port->attach != NULL) {
    port->attach(port->ctx, NULL, NULL);
  }
  return NULL;
}

err_h data_stream_connect(const data_stream_link_t* link) {
  ERR_CHECK(link != NULL, ERR_DATA_MATRIX_BAD_ARG);
  bool ok = false;
  bool full = false;
  portENTER_CRITICAL(&s_lock);
  const uint8_t from = find(link->from);
  const uint8_t to = find(link->to);
  ok = from != NO_PORT && to != NO_PORT && from != to && s_slots[from].port->attach != NULL &&
       s_slots[to].port->pass != NULL;
  if (ok) {
    slot_t* s = &s_slots[from];
    bool there = false;
    for (uint8_t k = 0; k < s->count; k++) {
      there |= s->to[k] == to;
    }
    full = !there && s->count == CONFIG_DATA_STREAM_TARGETS;
    if (!there && !full) {
      s->to[s->count++] = to;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(ok, ERR_DATA_MATRIX_BAD_ARG);
  ERR_CHECK(!full, ERR_DATA_MATRIX_FULL);
  return NULL;
}

err_h data_stream_disconnect(const data_stream_link_t* link) {
  ERR_CHECK(link != NULL, ERR_DATA_MATRIX_BAD_ARG);
  portENTER_CRITICAL(&s_lock);
  const uint8_t from = find(link->from);
  const uint8_t to = find(link->to);
  if (from != NO_PORT && to != NO_PORT) {
    slot_t* s = &s_slots[from];
    for (uint8_t k = 0; k < s->count; k++) {
      if (s->to[k] == to) {
        memmove(&s->to[k], &s->to[k + 1], (size_t)(s->count - k - 1));
        s->count--;
        break;
      }
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return NULL;
}
