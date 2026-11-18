/**
 * @file data_stream.c
 * @brief The switch of the stream ports (data_stream.h): a table of ports, a table of links, and out() that hands a
 * packet unchanged to every port linked from its sender. NOT IN THE BUILD yet (DATA_MATRIX.MD "data_stream").
 *
 * The tables change under a spinlock; out() copies its targets onto the stack under it and calls their inputs after
 * it, so a port's pass never runs under the lock and a link removed meanwhile still gets the packet in flight. The
 * attach calls run after the lock too: the tables are changed from one task (boot, the commands), so a port is never
 * attached and detached at once; out() may run on any task.
 */

#include "data_stream.h"

#include <stdbool.h>
#include <string.h>
#include "data_matrix_errors.h"
#include "freertos/FreeRTOS.h"

#ifndef CONFIG_DATA_STREAM_PORTS
#define CONFIG_DATA_STREAM_PORTS 32  // the bitmask of the cycle check is a uint32_t: at most 32
#endif
#ifndef CONFIG_DATA_STREAM_LINKS
#define CONFIG_DATA_STREAM_LINKS 24
#endif

_Static_assert(CONFIG_DATA_STREAM_PORTS <= 32, "the cycle check keeps the ports in a uint32_t");

#define NO_PORT 0xFFu

// A link by the ports' places in the table.
typedef struct {
  uint8_t from;
  uint8_t to;
} link_t;

static struct {
  const data_stream_port_t* ports[CONFIG_DATA_STREAM_PORTS];  // NULL: a free place
  link_t links[CONFIG_DATA_STREAM_LINKS];
  uint32_t link_count;
} s_ds;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

static bool same_id(data_stream_port_id_t a, data_stream_port_id_t b) {
  return a.kind == b.kind && a.id == b.id;
}

// The place of a registered port, NO_PORT when it is not. Under the lock.
static uint8_t find(data_stream_port_id_t id) {
  for (uint8_t i = 0; i < CONFIG_DATA_STREAM_PORTS; i++) {
    if (s_ds.ports[i] != NULL && same_id(s_ds.ports[i]->id, id)) {
      return i;
    }
  }
  return NO_PORT;
}

// Whether port `at` is the source of any link. Under the lock.
static bool is_source(uint8_t at) {
  for (uint32_t i = 0; i < s_ds.link_count; i++) {
    if (s_ds.links[i].from == at) {
      return true;
    }
  }
  return false;
}

// Whether `to` reaches `from` along the links: then from -> to closes a cycle. Under the lock.
static bool reaches(uint8_t to, uint8_t from) {
  uint32_t seen = 1u << to;
  uint8_t stack[CONFIG_DATA_STREAM_PORTS];
  uint32_t depth = 0;
  stack[depth++] = to;
  while (depth > 0) {
    const uint8_t at = stack[--depth];
    if (at == from) {
      return true;
    }
    for (uint32_t i = 0; i < s_ds.link_count; i++) {
      const uint8_t next = s_ds.links[i].to;
      if (s_ds.links[i].from == at && (seen & (1u << next)) == 0) {
        seen |= 1u << next;
        stack[depth++] = next;
      }
    }
  }
  return false;
}

// The output the switch gives a source: `link` is its place in the table.
static void out(void* link, const uint8_t* data, uint32_t len) {
  const uint8_t at = (uint8_t)(uintptr_t)link;
  const data_stream_port_t* targets[CONFIG_DATA_STREAM_LINKS];
  data_stream_port_id_t from = {0};
  uint32_t n = 0;
  portENTER_CRITICAL(&s_lock);
  if (s_ds.ports[at] != NULL) {
    from = s_ds.ports[at]->id;
    for (uint32_t i = 0; i < s_ds.link_count; i++) {
      if (s_ds.links[i].from == at) {
        targets[n++] = s_ds.ports[s_ds.links[i].to];
      }
    }
  }
  portEXIT_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < n; i++) {
    targets[i]->pass(targets[i]->ctx, from, data, len);
  }
}

err_h data_stream_register(const data_stream_port_t* port) {
  ERR_CHECK(port != NULL && (port->pass != NULL || port->attach != NULL), ERR_DATA_MATRIX_BAD_ARG);
  bool taken = false;
  uint8_t at = NO_PORT;
  portENTER_CRITICAL(&s_lock);
  taken = find(port->id) != NO_PORT;
  for (uint8_t i = 0; !taken && at == NO_PORT && i < CONFIG_DATA_STREAM_PORTS; i++) {
    at = s_ds.ports[i] == NULL ? i : NO_PORT;
  }
  if (!taken && at != NO_PORT) {
    s_ds.ports[at] = port;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(!taken, ERR_DATA_MATRIX_BAD_ARG);
  ERR_CHECK(at != NO_PORT, ERR_DATA_MATRIX_FULL);
  return NULL;
}

err_h data_stream_unregister(data_stream_port_id_t id) {
  const data_stream_port_t* detach[CONFIG_DATA_STREAM_PORTS];  // sources left with no link: the port itself too
  uint32_t n = 0;
  bool known = false;
  portENTER_CRITICAL(&s_lock);
  const uint8_t at = find(id);
  known = at != NO_PORT;
  if (known) {
    bool was_source[CONFIG_DATA_STREAM_PORTS];
    for (uint8_t i = 0; i < CONFIG_DATA_STREAM_PORTS; i++) {
      was_source[i] = s_ds.ports[i] != NULL && is_source(i);
    }
    uint32_t kept = 0;
    for (uint32_t i = 0; i < s_ds.link_count; i++) {
      if (s_ds.links[i].from != at && s_ds.links[i].to != at) {
        s_ds.links[kept++] = s_ds.links[i];
      }
    }
    s_ds.link_count = kept;
    for (uint8_t i = 0; i < CONFIG_DATA_STREAM_PORTS; i++) {
      if (was_source[i] && (i == at || !is_source(i))) {
        detach[n++] = s_ds.ports[i];
      }
    }
    s_ds.ports[at] = NULL;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(known, ERR_DATA_MATRIX_BAD_ARG);
  for (uint32_t i = 0; i < n; i++) {
    detach[i]->attach(detach[i]->ctx, NULL, NULL);
  }
  return NULL;
}

err_h data_stream_connect(const data_stream_link_t* link) {
  ERR_CHECK(link != NULL, ERR_DATA_MATRIX_BAD_ARG);
  bool ok = false;
  bool full = false;
  bool first = false;
  const data_stream_port_t* source = NULL;
  uint8_t from = NO_PORT;
  portENTER_CRITICAL(&s_lock);
  from = find(link->from);
  const uint8_t to = find(link->to);
  ok = from != NO_PORT && to != NO_PORT && from != to && s_ds.ports[from]->attach != NULL &&
       s_ds.ports[to]->pass != NULL;
  bool there = false;
  for (uint32_t i = 0; ok && i < s_ds.link_count; i++) {
    there |= s_ds.links[i].from == from && s_ds.links[i].to == to;
  }
  ok = ok && (there || !reaches(to, from));
  full = ok && !there && s_ds.link_count == CONFIG_DATA_STREAM_LINKS;
  if (ok && !there && !full) {
    first = !is_source(from);
    s_ds.links[s_ds.link_count++] = (link_t){.from = from, .to = to};
    source = s_ds.ports[from];
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(ok, ERR_DATA_MATRIX_BAD_ARG);
  ERR_CHECK(!full, ERR_DATA_MATRIX_FULL);
  if (first) {
    source->attach(source->ctx, out, (void*)(uintptr_t)from);
  }
  return NULL;
}

err_h data_stream_disconnect(const data_stream_link_t* link) {
  ERR_CHECK(link != NULL, ERR_DATA_MATRIX_BAD_ARG);
  const data_stream_port_t* source = NULL;
  portENTER_CRITICAL(&s_lock);
  const uint8_t from = find(link->from);
  const uint8_t to = find(link->to);
  for (uint32_t i = 0; from != NO_PORT && to != NO_PORT && i < s_ds.link_count; i++) {
    if (s_ds.links[i].from == from && s_ds.links[i].to == to) {
      memmove(&s_ds.links[i], &s_ds.links[i + 1], (s_ds.link_count - i - 1) * sizeof(link_t));
      s_ds.link_count--;
      source = is_source(from) ? NULL : s_ds.ports[from];
      break;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  if (source != NULL) {
    source->attach(source->ctx, NULL, NULL);
  }
  return NULL;
}

uint32_t data_stream_links(data_stream_link_t* out_links, uint32_t max) {
  portENTER_CRITICAL(&s_lock);
  const uint32_t n = s_ds.link_count;
  for (uint32_t i = 0; i < n && i < max; i++) {
    out_links[i] = (data_stream_link_t){.from = s_ds.ports[s_ds.links[i].from]->id,
                                        .to = s_ds.ports[s_ds.links[i].to]->id};
  }
  portEXIT_CRITICAL(&s_lock);
  return n;
}
