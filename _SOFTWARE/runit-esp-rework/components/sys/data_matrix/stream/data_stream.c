/**
 * @file data_stream.c
 * @brief The switch of the stream ports (data_stream.h): the registered ports, the links, and out() that hands a
 * packet unchanged to the input of every port linked from its sender. NOT IN THE BUILD yet (DATA_MATRIX.MD
 * "data_stream").
 *
 * The tables change under a spinlock. out() copies its targets onto the stack under it and calls them after it, so a
 * port's pass never runs under the lock.
 */

#include "data_stream.h"

#include <stdbool.h>
#include <string.h>
#include "data_matrix_errors.h"
#include "freertos/FreeRTOS.h"

#ifndef CONFIG_DATA_STREAM_PORTS
#define CONFIG_DATA_STREAM_PORTS 32
#endif
#ifndef CONFIG_DATA_STREAM_LINKS
#define CONFIG_DATA_STREAM_LINKS 24
#endif

static struct {
  const data_stream_port_t* ports[CONFIG_DATA_STREAM_PORTS];  // NULL: a free place
  data_stream_link_t links[CONFIG_DATA_STREAM_LINKS];
  uint32_t link_count;
} s_ds;

static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

// The registered port with this name, NULL when there is none. Under the lock.
static const data_stream_port_t* find(data_stream_port_id_t id) {
  for (uint32_t i = 0; i < CONFIG_DATA_STREAM_PORTS; i++) {
    if (s_ds.ports[i] != NULL && s_ds.ports[i]->id == id) {
      return s_ds.ports[i];
    }
  }
  return NULL;
}

// The output every port gets: `link` is the sending port.
static void out(void* link, const uint8_t* data, uint32_t len) {
  const data_stream_port_t* from = link;
  const data_stream_port_t* to[CONFIG_DATA_STREAM_LINKS];
  uint32_t n = 0;
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < s_ds.link_count; i++) {
    if (s_ds.links[i].from == from->id) {
      to[n++] = find(s_ds.links[i].to);
    }
  }
  portEXIT_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < n; i++) {
    to[i]->pass(to[i]->ctx, from->id, data, len);
  }
}

err_h data_stream_register(const data_stream_port_t* port) {
  ERR_CHECK(port != NULL && (port->pass != NULL || port->attach != NULL), ERR_DATA_MATRIX_BAD_ARG);
  bool taken = false;
  bool placed = false;
  portENTER_CRITICAL(&s_lock);
  taken = find(port->id) != NULL;
  for (uint32_t i = 0; !taken && !placed && i < CONFIG_DATA_STREAM_PORTS; i++) {
    if (s_ds.ports[i] == NULL) {
      s_ds.ports[i] = port;
      placed = true;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(!taken, ERR_DATA_MATRIX_BAD_ARG);
  ERR_CHECK(placed, ERR_DATA_MATRIX_FULL);
  if (port->attach != NULL) {
    port->attach(port->ctx, out, (void*)port);
  }
  return NULL;
}

err_h data_stream_unregister(data_stream_port_id_t id) {
  const data_stream_port_t* port = NULL;
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < CONFIG_DATA_STREAM_PORTS; i++) {
    if (s_ds.ports[i] != NULL && s_ds.ports[i]->id == id) {
      port = s_ds.ports[i];
      s_ds.ports[i] = NULL;
    }
  }
  uint32_t kept = 0;
  for (uint32_t i = 0; i < s_ds.link_count; i++) {
    if (s_ds.links[i].from != id && s_ds.links[i].to != id) {
      s_ds.links[kept++] = s_ds.links[i];
    }
  }
  s_ds.link_count = kept;
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
  const data_stream_port_t* from = find(link->from);
  const data_stream_port_t* to = find(link->to);
  ok = from != NULL && to != NULL && from != to && from->attach != NULL && to->pass != NULL;
  bool there = false;
  for (uint32_t i = 0; ok && i < s_ds.link_count; i++) {
    there |= s_ds.links[i].from == link->from && s_ds.links[i].to == link->to;
  }
  full = ok && !there && s_ds.link_count == CONFIG_DATA_STREAM_LINKS;
  if (ok && !there && !full) {
    s_ds.links[s_ds.link_count++] = *link;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(ok, ERR_DATA_MATRIX_BAD_ARG);
  ERR_CHECK(!full, ERR_DATA_MATRIX_FULL);
  return NULL;
}

err_h data_stream_disconnect(const data_stream_link_t* link) {
  ERR_CHECK(link != NULL, ERR_DATA_MATRIX_BAD_ARG);
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < s_ds.link_count; i++) {
    if (s_ds.links[i].from == link->from && s_ds.links[i].to == link->to) {
      memmove(&s_ds.links[i], &s_ds.links[i + 1], (s_ds.link_count - i - 1) * sizeof(s_ds.links[0]));
      s_ds.link_count--;
      break;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return NULL;
}
