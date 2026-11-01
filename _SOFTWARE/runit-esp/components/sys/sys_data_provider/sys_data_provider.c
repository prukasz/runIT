#include "sdkconfig.h"
#include "sys_data_provider_driver.h"
#include "sys_data_provider_internal.h"
#include "utils.h"

/* The registry: a slot per provider, under one spinlock. Nothing runs under it but a lookup and a copy: a driver's
 * send, its destroy and the user's rx are called after it is released. */
static U_SPINLOCK_DEFINE(s_lock);
static sys_data_provider_t* s_slots[CONFIG_SYS_DATA_PROVIDER_MAX];

#define LOCK()   U_CRITICAL_ENTER(s_lock)
#define UNLOCK() U_CRITICAL_EXIT(s_lock)

// The fields every tag but FULL carries: the provider's id.
#define ID_FIELDS(id) .link = (id).link, .channel = (id).channel

static bool same_id(sys_data_provider_id_t a, sys_data_provider_id_t b) {
  return a.link == b.link && a.channel == b.channel;
}

/* The provider of `id`, NULL when there is none. Under the lock. */
static sys_data_provider_t* find(sys_data_provider_id_t id) {
  for (uint32_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    if (s_slots[i] != NULL && same_id(s_slots[i]->id, id)) {
      return s_slots[i];
    }
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Use
 * ------------------------------------------------------------------------- */

err_h sys_data_provider_open(sys_data_provider_id_t id, sys_data_provider_rx_f rx, void* ctx) {
  LOCK();
  sys_data_provider_t* p = find(id);
  const bool taken = p != NULL && (p->state & SYS_DATA_PROVIDER_STATE_OPEN);
  if (p != NULL && !taken) {
    p->rx = rx;
    p->ctx = ctx;
    p->state |= SYS_DATA_PROVIDER_STATE_OPEN;
  }
  UNLOCK();
  ERR_CHECK(p != NULL, ERR_PROV_NOT_FOUND, ID_FIELDS(id));
  ERR_CHECK(!taken, ERR_PROV_IN_USE, ID_FIELDS(id));
  return NULL;
}

err_h sys_data_provider_close(sys_data_provider_id_t id) {
  LOCK();
  sys_data_provider_t* p = find(id);
  if (p != NULL) {
    p->rx = NULL;
    p->ctx = NULL;
    p->state &= (uint8_t)~SYS_DATA_PROVIDER_STATE_OPEN;
  }
  UNLOCK();
  ERR_CHECK(p != NULL, ERR_PROV_NOT_FOUND, ID_FIELDS(id));
  return NULL;
}

err_h sys_data_provider_send(sys_data_provider_id_t id, const uint8_t* data, uint32_t len, uint8_t flags) {
  ERR_CHECK(data != NULL && len > 0, ERR_CMD_BAD_ARG);
  LOCK();
  sys_data_provider_t* p = find(id);
  const uint32_t max = p != NULL ? p->max_frame : 0;
  const bool ok = p != NULL && max > 0 && len <= max;
  if (ok) {
    p->busy++;  // remove waits for it: the provider stays while its driver sends
  }
  UNLOCK();
  ERR_CHECK(p != NULL, ERR_PROV_NOT_FOUND, ID_FIELDS(id));
  ERR_CHECK(max > 0, ERR_PROV_DOWN, ID_FIELDS(id));
  ERR_CHECK(len <= max, ERR_PROV_TOO_LONG, ID_FIELDS(id), .len = (uint16_t)len, .max = (uint16_t)max);

  err_h err = p->send(p, data, len, flags);
  LOCK();
  p->busy--;
  UNLOCK();
  if (err != NULL && err->tag != ERR_PROV_BUSY) {
    err = ERR_WRAP(err, ERR_PROV_SEND, ID_FIELDS(id));
  }
  return err;
}

uint32_t sys_data_provider_max_frame(sys_data_provider_id_t id) {
  LOCK();
  const sys_data_provider_t* p = find(id);
  const uint32_t max = p != NULL ? p->max_frame : 0;
  UNLOCK();
  return max;
}

uint8_t sys_data_provider_state(sys_data_provider_id_t id) {
  LOCK();
  const sys_data_provider_t* p = find(id);
  const uint8_t state = p != NULL ? p->state : 0;
  UNLOCK();
  return state;
}

/* ----------------------------------------------------------------------------
 * Drivers
 * ------------------------------------------------------------------------- */

err_h sys_data_provider_add(sys_data_provider_t* p) {
  ERR_CHECK(p != NULL && p->send != NULL && p->id.link != 0, ERR_CMD_BAD_ARG);
  p->state &= SYS_DATA_PROVIDER_STATE_FRAMED | SYS_DATA_PROVIDER_STATE_WIRED;  // the rest is the registry's
  p->rx = NULL;
  p->ctx = NULL;
  p->busy = 0;
  LOCK();
  const bool exists = find(p->id) != NULL;
  bool full = true;
  for (uint32_t i = 0; !exists && i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    if (s_slots[i] == NULL) {
      s_slots[i] = p;
      full = false;
      break;
    }
  }
  UNLOCK();
  ERR_CHECK(!exists, ERR_PROV_EXISTS, ID_FIELDS(p->id));
  ERR_CHECK(!full, ERR_PROV_FULL);
  return NULL;
}

err_h sys_data_provider_remove(sys_data_provider_id_t id) {
  LOCK();
  sys_data_provider_t* p = NULL;
  uint32_t at = 0;
  for (; at < CONFIG_SYS_DATA_PROVIDER_MAX; at++) {
    if (s_slots[at] != NULL && same_id(s_slots[at]->id, id)) {
      p = s_slots[at];
      break;
    }
  }
  const bool in_use = p != NULL && ((p->state & SYS_DATA_PROVIDER_STATE_OPEN) || p->busy > 0);
  const bool system = p != NULL && (p->state & SYS_DATA_PROVIDER_STATE_SYSTEM);
  if (p != NULL && !in_use && !system) {
    s_slots[at] = NULL;
  }
  UNLOCK();
  ERR_CHECK(p != NULL, ERR_PROV_NOT_FOUND, ID_FIELDS(id));
  ERR_CHECK(!system, ERR_PROV_SYSTEM, ID_FIELDS(id));
  ERR_CHECK(!in_use, ERR_PROV_IN_USE, ID_FIELDS(id));
  if (p->destroy != NULL) {
    p->destroy(p);
  }
  return NULL;
}

void sys_data_provider_mark_system(void) {
  LOCK();
  for (uint32_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    if (s_slots[i] != NULL) {
      s_slots[i]->state |= SYS_DATA_PROVIDER_STATE_SYSTEM;
    }
  }
  UNLOCK();
}

void sys_data_provider_set_max_frame(sys_data_provider_t* p, uint32_t max) {
  LOCK();
  const bool changed = p->max_frame != max;
  p->max_frame = max;
  const sys_data_provider_rx_f rx = p->rx;
  void* ctx = p->ctx;
  UNLOCK();
  if (changed && rx != NULL) {
    rx(ctx, p->id, NULL, max);
  }
}

void sys_data_provider_deliver(sys_data_provider_t* p, const uint8_t* data, uint32_t len) {
  LOCK();
  const sys_data_provider_rx_f rx = p->rx;
  void* ctx = p->ctx;
  UNLOCK();
  if (rx != NULL && data != NULL && len > 0) {
    rx(ctx, p->id, data, len);
  }
}

/* ----------------------------------------------------------------------------
 * The router's
 * ------------------------------------------------------------------------- */

uint8_t sys_data_provider_list(sys_data_provider_info_t out[CONFIG_SYS_DATA_PROVIDER_MAX]) {
  uint8_t count = 0;
  LOCK();
  for (uint32_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    const sys_data_provider_t* p = s_slots[i];
    if (p != NULL) {
      out[count++] = (sys_data_provider_info_t){
          .id = p->id,
          .max_frame = (uint16_t)p->max_frame,
          .state = p->state,
      };
    }
  }
  UNLOCK();
  return count;
}

err_h sys_data_provider_send_free(sys_data_provider_id_t id, const uint8_t* data, uint32_t len) {
  LOCK();
  const sys_data_provider_t* p = find(id);
  const bool open = p != NULL && (p->state & SYS_DATA_PROVIDER_STATE_OPEN);
  UNLOCK();
  ERR_CHECK(p != NULL, ERR_PROV_NOT_FOUND, ID_FIELDS(id));
  ERR_CHECK(!open, ERR_PROV_IN_USE, ID_FIELDS(id));
  return sys_data_provider_send(id, data, len, 0);
}
