#include <stdbool.h>
#include <stdint.h>

#include "errors.h"
#include "sdkconfig.h"
#include "sys_ble.h"
#include "sys_data_provider_driver.h"

/** CREATE cfg: the characteristics a provider's frames go out on and come in from, 0 = none (at least one). */
typedef struct __attribute__((packed)) {
  uint16_t tx;  /**< Out: notify / indicate, with a TX buffer */
  uint16_t rx;  /**< In: written by the client */
} provider_ble_cfg_t;

typedef struct {
  uint16_t tx;
  uint16_t rx;
} provider_ble_t;

// Both characteristics' hooks: a write is one message (one frame on a framed one), NULL the link's state.
static void on_ble_rx(void* ctx, const uint8_t* data, uint32_t len) {
  if (data == NULL) {
    sys_data_provider_set_up(ctx, len != 0u);
    return;
  }
  sys_data_provider_deliver(ctx, data, len);
}

static err_h provider_ble_send(sys_data_provider_t* p, cmd_in_t frame, uint8_t flags) {
  const provider_ble_t* ctx = p->driver;
  const uint32_t wait_ms = (flags & CMD_SEND_NOWAIT) ? 0u : CONFIG_PROVIDER_BLE_SEND_WAIT_MS;
  err_h err = sys_ble_send(ctx->tx, frame, flags, wait_ms);
  if (err != NULL && err_root_tag(err) == ERR_BLE_TX_FULL) {
    err_free(err);
    return ERR_NEW(ERR_PROV_BUSY, .provider = p->id);
  }
  return err;
}

static err_h provider_ble_remove(sys_data_provider_t* p, void* ctx_in) {
  const provider_ble_t* ctx = ctx_in;
  err_h err = ctx->tx != 0 ? sys_ble_hook(ctx->tx, NULL, p, NULL) : NULL;
  if (err == NULL && ctx->rx != 0) {
    err = sys_ble_hook(ctx->rx, NULL, p, NULL);
  }
  return err;
}

// Framed when its characteristics are: a session's way. Up while a client is connected: the hooks tell it.
static err_h provider_ble_create(sys_data_provider_t* p, void* ctx_in, const void* cfg_in) {
  provider_ble_t* ctx = ctx_in;
  const provider_ble_cfg_t* cfg = cfg_in;
  ERR_CHECK(cfg->tx != 0 || cfg->rx != 0, ERR_CMD_BAD_ARG);
  bool rx_framed = true;
  bool tx_framed = true;
  if (cfg->rx != 0) {
    ERR_TRY(sys_ble_hook(cfg->rx, on_ble_rx, p, &rx_framed));
  }
  err_h err = cfg->tx != 0 ? sys_ble_hook(cfg->tx, on_ble_rx, p, &tx_framed) : NULL;
  if (err != NULL) {
    err_free(cfg->rx != 0 ? sys_ble_hook(cfg->rx, NULL, p, NULL) : NULL);  // a failed create is not removed
    return err;
  }
  *ctx = (provider_ble_t){.tx = cfg->tx, .rx = cfg->rx};
  p->send = provider_ble_send;
  p->state |= rx_framed && tx_framed ? SYS_DATA_PROVIDER_STATE_FRAMED : 0u;
  return NULL;
}

SYS_DATA_PROVIDER_TYPE_DEFINE(SYS_DATA_PROVIDER_TYPE_BLE, provider_ble_cfg_t, provider_ble_t, provider_ble_create,
                              provider_ble_remove, NULL, 0)
