/**
 * @file data_stream.c
 * @brief The switch of the stream ports (data_stream.h): the registered ports, each with the ports its output goes
 * to, and the `out` every registered port gets, which hands a packet unchanged to their inputs (DATA_MATRIX.MD).
 *
 * A port's targets are its own (at most CONFIG_DATA_STREAM_TARGETS, as places in the table), so out() only copies
 * them under the spinlock, a few bytes, and calls them after it: a port's pass never runs under the lock.
 */

#include "data_stream.h"

#include "data_matrix_errors.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include "sys_data_provider.h"

ERR_MODULE_DEFINE(MODULE_ID_DATA_MATRIX, ERR_DATA_MATRIX_MAP)

_Static_assert(CONFIG_DATA_STREAM_PORTS < 0xFF, "a port's place is a u8, 0xFF is none");

#define NO_PORT 0xFFu

// A registered port and where its output goes: the places of its targets.
typedef struct {
  data_stream_port_t* port;  // NULL: a free place
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

// The output every registered port gets (data_stream_send calls it).
static void out(data_stream_port_t* self, const uint8_t* data, uint32_t len) {
  data_stream_port_t* to[CONFIG_DATA_STREAM_TARGETS];
  uint32_t n = 0;
  portENTER_CRITICAL(&s_lock);
  const slot_t* from = &s_slots[self->place];
  if (from->port == self) {
    for (n = 0; n < from->count; n++) {
      to[n] = s_slots[from->to[n]].port;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < n; i++) {
    to[i]->pass(to[i], self->id, data, len);
  }
}

err_h data_stream_register(data_stream_port_t* port) {
  ERR_CHECK(port != NULL, ERR_DATA_MATRIX_BAD_ARG);
  uint8_t at = NO_PORT;
  portENTER_CRITICAL(&s_lock);
  const bool taken = find(port->id) != NO_PORT;
  for (uint8_t i = 0; !taken && at == NO_PORT && i < CONFIG_DATA_STREAM_PORTS; i++) {
    if (s_slots[i].port == NULL) {
      s_slots[i] = (slot_t){.port = port};
      port->place = i;
      port->out = out;
      at = i;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(!taken, ERR_DATA_MATRIX_BAD_ARG);
  ERR_CHECK(at != NO_PORT, ERR_DATA_MATRIX_FULL);
  return NULL;
}

err_h data_stream_unregister(data_stream_port_t* port) {
  ERR_CHECK(port != NULL, ERR_DATA_MATRIX_BAD_ARG);
  bool known = false;
  portENTER_CRITICAL(&s_lock);
  const uint8_t at = port->place;
  known = at < CONFIG_DATA_STREAM_PORTS && s_slots[at].port == port;
  if (known) {
    s_slots[at] = (slot_t){0};
    port->out = NULL;
    port->place = NO_PORT;
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
  ERR_CHECK(known, ERR_DATA_MATRIX_BAD_ARG);
  return NULL;
}

err_h data_stream_connect(const data_stream_link_t* link) {
  ERR_CHECK(link != NULL, ERR_DATA_MATRIX_BAD_ARG);
  bool ok = false;
  bool full = false;
  portENTER_CRITICAL(&s_lock);
  const uint8_t from = find(link->from);
  const uint8_t to = find(link->to);
  ok = from != NO_PORT && to != NO_PORT && from != to && s_slots[to].port->pass != NULL;
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
  slot_t* s = from != NO_PORT ? &s_slots[from] : NULL;
  for (uint8_t k = 0; s != NULL && k < s->count; k++) {
    if (s_slots[s->to[k]].port->id == link->to) {
      s->to[k] = s->to[--s->count];  // the order of the targets is no promise
      break;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return NULL;
}

uint32_t data_stream_links(data_stream_link_t* out_links, uint32_t max) {
  uint32_t n = 0;
  portENTER_CRITICAL(&s_lock);
  for (uint8_t i = 0; i < CONFIG_DATA_STREAM_PORTS; i++) {
    for (uint8_t k = 0; s_slots[i].port != NULL && k < s_slots[i].count; k++, n++) {
      if (n < max) {
        out_links[n] = (data_stream_link_t){.from = s_slots[i].port->id, .to = s_slots[s_slots[i].to[k]].port->id};
      }
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return n;
}

// The providers' ports: made and removed with them (sys_data_provider_set_port_hooks).
static void provider_added(data_stream_port_t* port) {
  err_report(data_stream_register(port));
}

static void provider_removed(data_stream_port_t* port) {
  err_report(data_stream_unregister(port));
}

void data_stream_init(void) {
  sys_data_provider_set_port_hooks(provider_added, provider_removed);
}
