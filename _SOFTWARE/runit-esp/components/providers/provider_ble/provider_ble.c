/**
 * @file provider_ble.c
 * @brief The BLE provider (PROVIDER_BLE.MD): one characteristic of sys_ble as a provider, both ways. What the port takes
 * goes out on it as notifications (sys_ble_send), what the client writes on it goes out of the port. A side that is not
 * wanted is a link the data matrix does not have.
 */

#include <string.h>

#include "errors.h"
#include "provider_ble.h"
#include "sdkconfig.h"
#include "sys_ble.h"
#include "sys_data_provider_driver.h"

typedef struct {
  sys_data_provider_t base;
  uint16_t svc;
  uint16_t chr;
  bool hooked;        // it holds the characteristic's hook
  uint8_t rx_flags;   // CMD_SEND_* of what the client writes, out of the port
  bool own_svc;       // the provider made the service: it removes it
} provider_ble_t;

// The hook, on the BLE host task: a write is one message (one frame on a framed characteristic) out of the port with
// the cfg's flags; NULL the link's state.
static void on_ble_rx(void* ctx, const uint8_t* data, uint32_t len) {
  provider_ble_t* me = ctx;
  if (data == NULL) {
    sys_data_provider_set_up(&me->base, len != 0u);
    return;
  }
  data_matrix_send(&me->base.port, me->rx_flags, data, len);
}

// The port's input: the packet onto the characteristic with its flags (NOWAIT: no room, dropped at once; else
// CONFIG_PROVIDER_BLE_SEND_WAIT_MS on the sender's task). What the characteristic does not take is lost.
static void ble_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* data,
                   uint32_t len) {
  (void)from;
  const provider_ble_t* me = SYS_DATA_PROVIDER_OF(self, provider_ble_t);
  const uint32_t wait_ms = (flags & CMD_SEND_NOWAIT) ? 0u : CONFIG_PROVIDER_BLE_SEND_WAIT_MS;
  err_free(sys_ble_send(me->chr, (cmd_in_t){.data = data, .len = len}, flags, wait_ms));
}

// What the provider made, taken out again: the hook (after its call in flight), the characteristic, its service.
static void drop(provider_ble_t* me) {
  if (me->hooked) {
    err_report(sys_ble_hook(me->chr, NULL, me, NULL));
  }
  if (me->chr != 0) {
    err_report(sys_ble_char_remove(me->chr));
  }
  if (me->own_svc) {
    err_report(sys_ble_service_remove(me->svc));
  }
  sys_ble_apply();
}

static err_h ble_create(sys_data_provider_t* p, const void* cfg_in) {
  provider_ble_t* me = (provider_ble_t*)p;
  const provider_ble_cfg_t* cfg = cfg_in;
  ERR_CHECK(cfg->svc != 0 && cfg->chr != 0 && (cfg->flags & ~PROVIDER_BLE_FRAMED) == 0u &&
                memchr(cfg->name, '\0', sizeof(cfg->name)) != NULL,
            ERR_BAD_ARG);
  const uint8_t framed = (cfg->flags & PROVIDER_BLE_FRAMED) != 0u ? SYS_BLE_CHR_FRAMED : 0u;
  me->svc = cfg->svc;
  me->rx_flags = cfg->rx_flags;

  // A service that exists is joined (its characteristic is added), unless it is locked.
  err_h err = sys_ble_service_create(cfg->svc, true);
  me->own_svc = err == NULL;
  if (err != NULL && err_root_tag(err) == ERR_BLE_UUID_TAKEN) {
    err_free(err);
    err = NULL;
  }
  if (err == NULL) {
    err = sys_ble_char_create(cfg->svc, cfg->chr, SYS_BLE_CHR_WRITE | SYS_BLE_CHR_NOTIFY | framed,
                              SYS_BLE_TX_BYTES(CONFIG_PROVIDER_BLE_TX_ITEMS), cfg->name[0] != '\0' ? cfg->name : NULL);
    me->chr = err == NULL ? cfg->chr : 0;
  }
  if (err == NULL) {
    err = sys_ble_hook(cfg->chr, on_ble_rx, me, NULL);
    me->hooked = err == NULL;
  }
  if (err != NULL) {
    drop(me);  // a failed create is not removed
    return err;
  }
  sys_ble_apply();
  p->state |= framed != 0u ? SYS_DATA_PROVIDER_STATE_FRAMED : 0u;
  return NULL;
}

static void ble_remove(sys_data_provider_t* p) {
  drop((provider_ble_t*)p);
}

SYS_DATA_PROVIDER_TYPE_DEFINE(SYS_DATA_PROVIDER_TYPE_BLE, provider_ble_cfg_t, provider_ble_t, ble_create, ble_remove,
                              ble_in, NULL, 0)
