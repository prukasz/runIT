#include <stdbool.h>
#include <stdint.h>

#include "errors.h"
#include "sdkconfig.h"
#include "sys_data_provider_driver.h"
#include "sys_device.h"
#include "sys_uart_contract.h"

typedef struct __attribute__((packed)) {
  uint8_t device; /**< CONTRACT_UART device ID */
} provider_uart_cfg_t;

typedef struct {
  uint8_t dev_id;
} provider_uart_t;

static void on_uart_rx(void* ctx, const uint8_t* data, uint32_t len) {
  if (data == NULL) {
    sys_data_provider_set_up(ctx, len != 0u);
    return;
  }
  sys_data_provider_deliver(ctx, data, len);
}

// The port's input: the packet onto the UART's queue with its flags (NOWAIT: no room, dropped at once; else the wait
// below on the sender's task). What the UART does not take is lost.
static void provider_uart_pass(data_stream_port_t* self, data_stream_port_id_t from, uint8_t flags,
                               const uint8_t* data, uint32_t len) {
  (void)from;
  const provider_uart_t* ctx = SYS_DATA_PROVIDER_OF(self)->driver;
  const uint16_t wait_ms =
      CONFIG_PROVIDER_UART_SEND_WAIT_MS > SYS_UART_WAIT_MAX_MS ? SYS_UART_WAIT_MAX_MS : CONFIG_PROVIDER_UART_SEND_WAIT_MS;
  err_free(SYS_DEVICE_SET(ctx->dev_id, CONTRACT_UART, OP_SEND_FRAME, .frame = {.data = data, .len = len},
                          .wait_ms = wait_ms, .flags = flags));
}

static err_h provider_uart_create(sys_data_provider_t* p, void* ctx_in, const void* cfg_in) {
  provider_uart_t* ctx = ctx_in;
  const provider_uart_cfg_t* cfg = cfg_in;
  sys_uart_info_t info;
  ERR_TRY(SYS_DEVICE_GET(cfg->device, CONTRACT_UART, OP_INFO, &info));

  const bool framed = info.mode == SYS_UART_MODE_FRAMED;
  p->port.pass = provider_uart_pass;
  // A port is up as long as it exists: its hook hears only that it is removed.
  p->state |= (uint8_t)(SYS_DATA_PROVIDER_STATE_WIRED | SYS_DATA_PROVIDER_STATE_UP |
                        (framed ? SYS_DATA_PROVIDER_STATE_FRAMED : 0u));
  ctx->dev_id = cfg->device;

  return SYS_DEVICE_SET(cfg->device, CONTRACT_UART, OP_RX_HOOK, .cb = on_uart_rx, .ctx = p);
}

static err_h provider_uart_remove(sys_data_provider_t* p, void* ctx_in) {
  const provider_uart_t* ctx = ctx_in;
  return SYS_DEVICE_SET(ctx->dev_id, CONTRACT_UART, OP_RX_HOOK, .cb = NULL, .ctx = p);
}

SYS_DATA_PROVIDER_TYPE_DEFINE(SYS_DATA_PROVIDER_TYPE_UART, provider_uart_cfg_t, provider_uart_t, provider_uart_create,
                              provider_uart_remove, NULL, 0)
