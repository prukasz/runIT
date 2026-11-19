#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "errors.h"
#include "provider_ble.h"
#include "sdkconfig.h"
#include "sys_ble.h"
#include "sys_data_provider_driver.h"

typedef struct {
  uint16_t svc;
  uint16_t tx;
  uint16_t rx;
  bool own_svc;  // the provider made the service: it removes it
} provider_ble_t;

// Both characteristics' hooks: a write is one message (one frame on a framed one), NULL the link's state.
static void on_ble_rx(void* ctx, const uint8_t* data, uint32_t len) {
  if (data == NULL) {
    sys_data_provider_set_up(ctx, len != 0u);
    return;
  }
  sys_data_provider_deliver(ctx, data, len);
}

// The port's input: the packet onto the TX characteristic with its flags (NOWAIT: no room, dropped at once; else
// CONFIG_PROVIDER_BLE_SEND_WAIT_MS on the sender's task). What the characteristic does not take is lost.
static void provider_ble_pass(data_stream_port_t* self, data_stream_port_id_t from, uint8_t flags, const uint8_t* data,
                              uint32_t len) {
  (void)from;
  const provider_ble_t* ctx = SYS_DATA_PROVIDER_OF(self)->driver;
  const uint32_t wait_ms = (flags & CMD_SEND_NOWAIT) ? 0u : CONFIG_PROVIDER_BLE_SEND_WAIT_MS;
  err_free(sys_ble_send(ctx->tx, (cmd_in_t){.data = data, .len = len}, flags, wait_ms));
}

// What the provider made, taken out again: the hooks, the characteristics, the service it made.
static void drop(sys_data_provider_t* p, const provider_ble_t* ctx) {
  if (ctx->tx != 0) {
    err_free(sys_ble_hook(ctx->tx, NULL, p, NULL));
    err_free(sys_ble_char_remove(ctx->tx));
  }
  if (ctx->rx != 0) {
    err_free(sys_ble_hook(ctx->rx, NULL, p, NULL));
    err_free(sys_ble_char_remove(ctx->rx));
  }
  if (ctx->own_svc) {
    err_free(sys_ble_service_remove(ctx->svc));
  }
  sys_ble_apply();
}

static err_h provider_ble_remove(sys_data_provider_t* p, void* ctx_in) {
  drop(p, ctx_in);
  return NULL;
}

// The description of a characteristic: the name, with " TX" or " RX" after it when the provider has both.
static const char* describe(char* out, size_t cap, const provider_ble_cfg_t* cfg, const char* suffix) {
  if (cfg->name[0] == '\0') {
    return NULL;
  }
  const bool both = cfg->tx != 0 && cfg->rx != 0;
  snprintf(out, cap, "%s%s", cfg->name, both ? suffix : "");
  return out;
}

// A service that exists is joined (its characteristics are added), unless it is locked. Up while a client is connected:
// the hooks tell it.
static err_h make_service(provider_ble_t* ctx, uint16_t svc) {
  err_h err = sys_ble_service_create(svc, true);
  if (err != NULL && err_root_tag(err) == ERR_BLE_UUID_TAKEN) {
    err_free(err);
    return NULL;
  }
  ctx->own_svc = err == NULL;
  return err;
}

static err_h provider_ble_create(sys_data_provider_t* p, void* ctx_in, const void* cfg_in) {
  provider_ble_t* ctx = ctx_in;
  const provider_ble_cfg_t* cfg = cfg_in;
  ERR_CHECK(cfg->svc != 0 && (cfg->tx != 0 || cfg->rx != 0) && (cfg->flags & ~PROVIDER_BLE_FRAMED) == 0u &&
                memchr(cfg->name, '\0', sizeof(cfg->name)) != NULL,
            ERR_CMD_BAD_ARG);
  char text[sizeof(cfg->name) + 4];
  const uint8_t framed = (cfg->flags & PROVIDER_BLE_FRAMED) != 0u ? SYS_BLE_CHR_FRAMED : 0u;
  *ctx = (provider_ble_t){.svc = cfg->svc};
  ERR_TRY(make_service(ctx, cfg->svc));
  err_h err = cfg->rx != 0 ? sys_ble_char_create(cfg->svc, cfg->rx, SYS_BLE_CHR_WRITE | framed, 0,
                                                 describe(text, sizeof(text), cfg, " RX"))
                           : NULL;
  if (err == NULL) {
    ctx->rx = cfg->rx;
    err = cfg->tx != 0 ? sys_ble_char_create(cfg->svc, cfg->tx, SYS_BLE_CHR_NOTIFY | framed,
                                              SYS_BLE_TX_BYTES(CONFIG_PROVIDER_BLE_TX_ITEMS),
                                              describe(text, sizeof(text), cfg, " TX"))
                       : NULL;
  }
  if (err == NULL) {
    ctx->tx = cfg->tx;
    err = ctx->rx != 0 ? sys_ble_hook(ctx->rx, on_ble_rx, p, NULL) : NULL;
  }
  if (err == NULL && ctx->tx != 0) {
    err = sys_ble_hook(ctx->tx, on_ble_rx, p, NULL);
  }
  if (err != NULL) {
    drop(p, ctx);  // a failed create is not removed
    return err;
  }
  sys_ble_apply();
  p->port.pass = provider_ble_pass;
  p->state |= framed != 0u ? SYS_DATA_PROVIDER_STATE_FRAMED : 0u;
  return NULL;
}

SYS_DATA_PROVIDER_TYPE_DEFINE(SYS_DATA_PROVIDER_TYPE_BLE, provider_ble_cfg_t, provider_ble_t, provider_ble_create,
                              provider_ble_remove, NULL, 0)
