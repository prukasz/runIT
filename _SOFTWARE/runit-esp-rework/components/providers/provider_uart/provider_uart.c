/**
 * @file provider_uart.c
 * @brief The UART provider (PROVIDER_UART.MD): any CONTRACT_UART device as a provider. What the port takes goes onto
 * the UART (OP_SEND_FRAME), what the UART hears goes out of the port (OP_RX_HOOK).
 */

#include "errors.h"
#include "sdkconfig.h"
#include "sys_data_provider_driver.h"
#include "sys_device.h"
#include "sys_uart_contract.h"

_Static_assert(CONFIG_PROVIDER_UART_SEND_WAIT_MS <= SYS_UART_WAIT_MAX_MS, "PROVIDER_UART_SEND_WAIT_MS over the UART's");

/** @brief CREATE cfg: the UART device and how what it hears goes on. */
typedef struct __attribute__((packed)) {
  uint8_t device;  /**< A CONTRACT_UART device ID */
  uint8_t flags;   /**< CMD_SEND_* of each message out of the port (CMD_SEND_NOWAIT: the UART's RX never waits) */
} provider_uart_cfg_t;

typedef struct {
  sys_data_provider_t base;
  uint8_t device;  // SYS_DEVICE_ID_NONE once the UART is removed
  uint8_t flags;
} provider_uart_t;

// The UART's reader, on its RX task under the UART's lock: a message out of the port with the cfg's flags (without
// CMD_SEND_NOWAIT a taker may wait for room, and the UART's RX with it); NULL once, when the UART is removed: the
// provider forgets its number, so it never sends to a device made again under it.
static void on_rx(void* ctx, const uint8_t* data, uint32_t len) {
  provider_uart_t* me = ctx;
  if (data == NULL) {
    me->device = SYS_DEVICE_ID_NONE;
    sys_data_provider_set_up(&me->base, false);
    return;
  }
  data_matrix_send(&me->base.port, me->flags, data, len);
}

// The port's input: the packet onto the UART's queue with its flags (NOWAIT: no room, dropped at once; else it waits
// for room up to CONFIG_PROVIDER_UART_SEND_WAIT_MS). What the UART does not take is lost.
static void uart_in(data_matrix_port_t* self, data_matrix_port_id_t from, uint8_t flags, const uint8_t* data,
                    uint32_t len) {
  (void)from;
  const provider_uart_t* me = SYS_DATA_PROVIDER_OF(self, provider_uart_t);
  err_free(SYS_DEVICE_SET(me->device, CONTRACT_UART, OP_SEND_FRAME, .frame = {.data = data, .len = len},
                          .wait_ms = CONFIG_PROVIDER_UART_SEND_WAIT_MS, .flags = flags));
}

static err_h uart_create(sys_data_provider_t* p, const void* cfg_in) {
  const provider_uart_cfg_t* cfg = cfg_in;
  sys_uart_info_t info;
  ERR_TRY(SYS_DEVICE_GET(cfg->device, CONTRACT_UART, OP_INFO, &info));
  provider_uart_t* me = (provider_uart_t*)p;
  me->device = cfg->device;
  me->flags = cfg->flags;
  // A UART is up as long as it exists.
  p->state |= (uint8_t)(SYS_DATA_PROVIDER_STATE_WIRED | SYS_DATA_PROVIDER_STATE_UP |
                        (info.mode == SYS_UART_MODE_FRAMED ? SYS_DATA_PROVIDER_STATE_FRAMED : 0u));
  return SYS_DEVICE_SET(cfg->device, CONTRACT_UART, OP_RX_HOOK, .cb = on_rx, .ctx = p);
}

static void uart_remove(sys_data_provider_t* p) {
  // A UART removed before: its hook is gone with it.
  err_free(SYS_DEVICE_SET(((provider_uart_t*)p)->device, CONTRACT_UART, OP_RX_HOOK, .cb = NULL, .ctx = p));
}

SYS_DATA_PROVIDER_TYPE_DEFINE(SYS_DATA_PROVIDER_TYPE_UART, provider_uart_cfg_t, provider_uart_t, uart_create,
                              uart_remove, uart_in, NULL, 0)
