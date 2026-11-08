#include "cmd.h"
#include "sdkconfig.h"
#include "sys_data_provider_driver.h"
#include "sys_data_provider_internal.h"
#include "utils.h"

/* The registry: a slot per provider, under one spinlock. Nothing runs under it but a lookup and a copy: a driver's
 * send, its destroy and the user's rx are called after it is released. */
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;
static sys_data_provider_t* s_slots[CONFIG_SYS_DATA_PROVIDER_MAX];

// The fields every tag but FULL carries: the provider's id.
#define ID_FIELDS(id) .provider = (id)

/* The provider of `id`, NULL when there is none. Under the lock. */
static sys_data_provider_t* find(uint8_t id) {
  for (uint32_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    if (s_slots[i] != NULL && s_slots[i]->id == id) {
      return s_slots[i];
    }
  }
  return NULL;
}

/* ----------------------------------------------------------------------------
 * Use
 * ------------------------------------------------------------------------- */

err_h sys_data_provider_open(uint8_t id, sys_data_provider_rx_f rx, void* ctx) {
  portENTER_CRITICAL(&s_lock);
  sys_data_provider_t* p = find(id);
  const bool taken = p != NULL && (p->state & SYS_DATA_PROVIDER_STATE_OPEN);
  const bool no_rx = p != NULL && rx != NULL && (p->state & SYS_DATA_PROVIDER_STATE_NO_RX);
  if (p != NULL && !taken && !no_rx) {
    p->rx = rx;
    p->ctx = ctx;
    p->state |= SYS_DATA_PROVIDER_STATE_OPEN;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(p != NULL, ERR_PROV_NOT_FOUND, ID_FIELDS(id));
  ERR_CHECK(!taken, ERR_PROV_IN_USE, ID_FIELDS(id));
  ERR_CHECK(!no_rx, ERR_PROV_NO_RX, ID_FIELDS(id));
  return NULL;
}

err_h sys_data_provider_close(uint8_t id) {
  portENTER_CRITICAL(&s_lock);
  sys_data_provider_t* p = find(id);
  if (p != NULL) {
    p->rx = NULL;
    p->ctx = NULL;
    p->state &= (uint8_t)~SYS_DATA_PROVIDER_STATE_OPEN;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(p != NULL, ERR_PROV_NOT_FOUND, ID_FIELDS(id));
  return NULL;
}

err_h sys_data_provider_send(uint8_t id, const uint8_t* data, uint32_t len, uint8_t flags) {
  ERR_CHECK(data != NULL && len > 0, ERR_CMD_BAD_ARG);
  portENTER_CRITICAL(&s_lock);
  sys_data_provider_t* p = find(id);
  const bool up = p != NULL && (p->state & SYS_DATA_PROVIDER_STATE_UP);
  if (up && len <= CMD_FRAME_MAX) {
    p->busy++;  // remove waits for it: the provider stays while its driver sends
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(p != NULL, ERR_PROV_NOT_FOUND, ID_FIELDS(id));
  ERR_CHECK(up, ERR_PROV_DOWN, ID_FIELDS(id));
  ERR_CHECK(len <= CMD_FRAME_MAX, ERR_PROV_TOO_LONG, ID_FIELDS(id), .len = (uint16_t)len, .max = CMD_FRAME_MAX);

  err_h err = p->send(p, data, len, flags);
  portENTER_CRITICAL(&s_lock);
  p->busy--;
  portEXIT_CRITICAL(&s_lock);
  if (err != NULL && err->tag != ERR_PROV_BUSY) {
    err = ERR_WRAP(err, ERR_PROV_SEND, ID_FIELDS(id));
  }
  return err;
}

uint8_t sys_data_provider_state(uint8_t id) {
  portENTER_CRITICAL(&s_lock);
  const sys_data_provider_t* p = find(id);
  const uint8_t state = p != NULL ? p->state : 0;
  portEXIT_CRITICAL(&s_lock);
  return state;
}

/* ----------------------------------------------------------------------------
 * Drivers
 * ------------------------------------------------------------------------- */

err_h sys_data_provider_add(sys_data_provider_t* p) {
  ERR_CHECK(p != NULL && p->send != NULL, ERR_CMD_BAD_ARG);
  p->state &= (uint8_t)~SYS_DATA_PROVIDER_STATE_OPEN;  // that one is the registry's
  p->rx = NULL;
  p->ctx = NULL;
  p->busy = 0;
  portENTER_CRITICAL(&s_lock);
  const bool exists = find(p->id) != NULL;
  bool full = true;
  for (uint32_t i = 0; !exists && i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    if (s_slots[i] == NULL) {
      s_slots[i] = p;
      full = false;
      break;
    }
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(!exists, ERR_PROV_EXISTS, ID_FIELDS(p->id));
  ERR_CHECK(!full, ERR_PROV_FULL);
  return NULL;
}

err_h sys_data_provider_remove(uint8_t id) {
  portENTER_CRITICAL(&s_lock);
  sys_data_provider_t* p = NULL;
  uint32_t at = 0;
  for (; at < CONFIG_SYS_DATA_PROVIDER_MAX; at++) {
    if (s_slots[at] != NULL && s_slots[at]->id == id) {
      p = s_slots[at];
      break;
    }
  }
  const bool in_use = p != NULL && ((p->state & SYS_DATA_PROVIDER_STATE_OPEN) || p->busy > 0);
  const bool system = p != NULL && (p->state & SYS_DATA_PROVIDER_STATE_SYSTEM);
  if (p != NULL && !in_use && !system) {
    s_slots[at] = NULL;
  }
  portEXIT_CRITICAL(&s_lock);
  ERR_CHECK(p != NULL, ERR_PROV_NOT_FOUND, ID_FIELDS(id));
  ERR_CHECK(!system, ERR_PROV_SYSTEM, ID_FIELDS(id));
  ERR_CHECK(!in_use, ERR_PROV_IN_USE, ID_FIELDS(id));
  if (p->destroy != NULL) {
    p->destroy(p);
  }
  return NULL;
}

void sys_data_provider_set_up(sys_data_provider_t* p, bool up) {
  portENTER_CRITICAL(&s_lock);
  const bool changed = ((p->state & SYS_DATA_PROVIDER_STATE_UP) != 0) != up;
  p->state = up ? (uint8_t)(p->state | SYS_DATA_PROVIDER_STATE_UP) : (uint8_t)(p->state & ~SYS_DATA_PROVIDER_STATE_UP);
  const sys_data_provider_rx_f rx = p->rx;
  void* ctx = p->ctx;
  portEXIT_CRITICAL(&s_lock);
  if (changed && rx != NULL) {
    rx(ctx, p->id, NULL, up ? 1u : 0u);
  }
}

void sys_data_provider_deliver(sys_data_provider_t* p, const uint8_t* data, uint32_t len) {
  portENTER_CRITICAL(&s_lock);
  const sys_data_provider_rx_f rx = p->rx;
  void* ctx = p->ctx;
  portEXIT_CRITICAL(&s_lock);
  if (rx != NULL && data != NULL && len > 0) {
    rx(ctx, p->id, data, len);
  }
}

/* ----------------------------------------------------------------------------
 * The router's
 * ------------------------------------------------------------------------- */

uint8_t sys_data_provider_list(sys_data_provider_info_t out[CONFIG_SYS_DATA_PROVIDER_MAX]) {
  uint8_t count = 0;
  portENTER_CRITICAL(&s_lock);
  for (uint32_t i = 0; i < CONFIG_SYS_DATA_PROVIDER_MAX; i++) {
    const sys_data_provider_t* p = s_slots[i];
    if (p != NULL) {
      out[count++] = (sys_data_provider_info_t){
          .provider = p->id,
          .link = p->link,
          .state = p->state,
      };
    }
  }
  portEXIT_CRITICAL(&s_lock);
  return count;
}

