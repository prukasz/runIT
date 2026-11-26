/**
 * @file data_matrix.c
 * @brief The switch of the ports (data_matrix.h): the registered ports, each with the ports its output goes to, and
 * the sends, which hand a packet unchanged to their inputs (all of them, or all but one) or to one port by its name
 * (DATA_MATRIX.MD).
 *
 * A port's targets are its own (at most CONFIG_DATA_MATRIX_TARGETS, as places in the table), so a send only copies
 * them under the spinlock, a few bytes, and calls them after it: a port's `in` never runs under the lock. What
 * data_matrix_linked reads without the lock (a slot's `port` and `count`, a port's `place`) is written with atomic
 * stores, under the lock.
 */

#include "data_matrix.h"

#include <string.h>

#include "cmd.h"

#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"

_Static_assert(CONFIG_DATA_MATRIX_PORTS < 0xFF, "a port's place is a u8, 0xFF is none");

#define PORT_NONE 0xFFu

// A registered port and where its output goes: the places of its targets. A free place has `count` 0.
typedef struct {
  data_matrix_port_t* port;  // NULL: a free place
  uint8_t to[CONFIG_DATA_MATRIX_TARGETS];
  uint8_t count;
} slot_t;

#define STORE(field, value) __atomic_store_n(&(field), (value), __ATOMIC_RELAXED)

static slot_t s_slots[CONFIG_DATA_MATRIX_PORTS];
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

// The place of the registered port with this name, PORT_NONE when there is none. Under the lock.
static uint8_t find(data_matrix_port_id_t id) {
  for (uint8_t i = 0; i < CONFIG_DATA_MATRIX_PORTS; i++) {
    if (s_slots[i].port != NULL && s_slots[i].port->id == id) {
      return i;
    }
  }
  return PORT_NONE;
}

// The ports `self`'s output goes to, copied under the lock into `to`; 0 when `self` is not registered.
static uint32_t targets(const data_matrix_port_t* self, data_matrix_port_t* to[CONFIG_DATA_MATRIX_TARGETS]) {
  uint32_t n = 0;
  portENTER_CRITICAL(&s_lock);
  const slot_t* from = self->place < CONFIG_DATA_MATRIX_PORTS ? &s_slots[self->place] : NULL;
  if (from != NULL && from->port == self) {  // registered
    for (n = 0; n < from->count; n++) {
      to[n] = s_slots[from->to[n]].port;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return n;
}

void data_matrix_send(data_matrix_port_t* self, uint8_t flags, const uint8_t* data, uint32_t len) {
  data_matrix_port_t* to[CONFIG_DATA_MATRIX_TARGETS];
  const uint32_t n = targets(self, to);
  for (uint32_t i = 0; i < n; i++) {
    to[i]->in(to[i], self->id, flags, data, len);
  }
}

// Without the lock: the answer is a snapshot either way, and a send copies the targets under it again.
bool data_matrix_linked(const data_matrix_port_t* self) {
  const uint8_t at = __atomic_load_n(&self->place, __ATOMIC_RELAXED);
  return at < CONFIG_DATA_MATRIX_PORTS && __atomic_load_n(&s_slots[at].port, __ATOMIC_RELAXED) == self &&
         __atomic_load_n(&s_slots[at].count, __ATOMIC_RELAXED) != 0u;
}

void data_matrix_push(data_matrix_port_t* port, uint8_t urgent, const void* head, uint32_t head_len, const void* data,
                      uint32_t len) {
  const uint32_t size = CMD_HEAD + head_len + len;
  if (size > CMD_FRAME_MAX || !data_matrix_linked(port)) {
    return;
  }
  uint8_t packet[size];  // at most one frame
  cmd_push_head(packet, (uint8_t)port->id, urgent);
  if (head_len > 0) {
    memcpy(packet + CMD_HEAD, head, head_len);
  }
  memcpy(packet + CMD_HEAD + head_len, data, len);
  data_matrix_send(port, (uint8_t)(CMD_SEND_NOWAIT | urgent), packet, size);
}

bool data_matrix_send_to(data_matrix_port_t* self, data_matrix_port_id_t target, uint8_t flags, const uint8_t* data,
                         uint32_t len) {
  portENTER_CRITICAL(&s_lock);
  const uint8_t at = find(target);
  data_matrix_port_t* to = at != PORT_NONE ? s_slots[at].port : NULL;
  portEXIT_CRITICAL(&s_lock);
  if (to == NULL || to->in == NULL) {
    return false;
  }
  to->in(to, self->id, flags, data, len);
  return true;
}

void data_matrix_send_except(data_matrix_port_t* self, data_matrix_port_id_t except, uint8_t flags,
                             const uint8_t* data, uint32_t len) {
  data_matrix_port_t* to[CONFIG_DATA_MATRIX_TARGETS];
  const uint32_t n = targets(self, to);
  for (uint32_t i = 0; i < n; i++) {
    if (to[i]->id != except) {
      to[i]->in(to[i], self->id, flags, data, len);
    }
  }
}

err_h data_matrix_register(data_matrix_port_t* port) {
  ERR_CHECK(port != NULL, ERR_BAD_ARG);
  uint8_t at = PORT_NONE;
  portENTER_CRITICAL(&s_lock);
  const bool taken = find(port->id) != PORT_NONE;
  for (uint8_t i = 0; !taken && at == PORT_NONE && i < CONFIG_DATA_MATRIX_PORTS; i++) {
    if (s_slots[i].port == NULL) {  // its `count` is 0
      STORE(s_slots[i].port, port);
      STORE(port->place, i);
      at = i;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(!taken, ERR_BAD_ARG);
  ERR_CHECK(at != PORT_NONE, ERR_FULL, .max = CONFIG_DATA_MATRIX_PORTS);
  return NULL;
}

err_h data_matrix_unregister(data_matrix_port_t* port) {
  ERR_CHECK(port != NULL, ERR_BAD_ARG);
  bool known = false;
  portENTER_CRITICAL(&s_lock);
  const uint8_t at = port->place;
  known = at < CONFIG_DATA_MATRIX_PORTS && s_slots[at].port == port;
  if (known) {
    STORE(s_slots[at].count, 0u);
    STORE(s_slots[at].port, NULL);
    STORE(port->place, (uint8_t)PORT_NONE);
    for (uint8_t i = 0; i < CONFIG_DATA_MATRIX_PORTS; i++) {  // the links to it go with it
      slot_t* s = &s_slots[i];
      uint8_t kept = 0;
      for (uint8_t k = 0; k < s->count; k++) {
        if (s->to[k] != at) {
          s->to[kept++] = s->to[k];
        }
      }
      STORE(s->count, kept);
    }
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(known, ERR_BAD_ARG);
  return NULL;
}

err_h data_matrix_connect(const data_matrix_link_t* link) {
  ERR_CHECK(link != NULL, ERR_BAD_ARG);
  bool ok = false;
  bool full = false;
  portENTER_CRITICAL(&s_lock);
  const uint8_t from = find(link->from);
  const uint8_t to = find(link->to);
  ok = from != PORT_NONE && to != PORT_NONE && from != to && s_slots[to].port->in != NULL;
  if (ok) {
    slot_t* s = &s_slots[from];
    bool there = false;
    for (uint8_t k = 0; k < s->count; k++) {
      there |= s->to[k] == to;
    }
    full = !there && s->count == CONFIG_DATA_MATRIX_TARGETS;
    if (!there && !full) {
      s->to[s->count] = to;
      STORE(s->count, (uint8_t)(s->count + 1u));
    }
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(ok, ERR_BAD_ARG);
  ERR_CHECK(!full, ERR_FULL, .max = CONFIG_DATA_MATRIX_TARGETS);
  return NULL;
}

err_h data_matrix_disconnect(const data_matrix_link_t* link) {
  ERR_CHECK(link != NULL, ERR_BAD_ARG);
  portENTER_CRITICAL(&s_lock);
  const uint8_t from = find(link->from);
  slot_t* s = from != PORT_NONE ? &s_slots[from] : NULL;
  for (uint8_t k = 0; s != NULL && k < s->count; k++) {
    if (s_slots[s->to[k]].port->id == link->to) {
      s->to[k] = s->to[s->count - 1u];  // the order of the targets is no promise
      STORE(s->count, (uint8_t)(s->count - 1u));
      break;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return NULL;
}

uint32_t data_matrix_links(data_matrix_link_t* out_links, uint32_t max) {
  uint32_t n = 0;
  portENTER_CRITICAL(&s_lock);
  for (uint8_t i = 0; i < CONFIG_DATA_MATRIX_PORTS; i++) {
    for (uint8_t k = 0; s_slots[i].port != NULL && k < s_slots[i].count; k++, n++) {
      if (n < max) {
        out_links[n] = (data_matrix_link_t){.from = s_slots[i].port->id, .to = s_slots[s_slots[i].to[k]].port->id};
      }
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return n;
}
