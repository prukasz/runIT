#include "sys_uart_contract.h"

ERR_MODULE_DEFINE(ERR_MOD_SYS_UART, "sys_uart", ERR_SYS_UART_MAP)
SYS_DEVICE_CONTRACT_DEFINE(UART, 0x13)

/* ----------------------------------------------------------------------------
 * A driver on a port
 * ------------------------------------------------------------------------- */

// The owner's resource: the subscription let go (in the owner's REMOVE, so the caller is the owner); none afterwards
// even when the port failed.
static err_h sub_release(void* arg) {
  sys_uart_port_t* sub = arg;
  if (!sub->subscribed) {
    return NULL;
  }
  sub->subscribed = 0;
  return SYS_DEVICE_SET(sub->port, CONTRACT_UART, OP_SUBSCRIBE, .op = 0);
}

err_h sys_uart_port_subscribe(uint8_t owner, sys_uart_port_t* sub, uint8_t port, uint16_t op) {
  ERR_CHECK(sub != NULL && op != 0, ERR_UART_BAD_ARG);
  sub->port = port;
  ERR_TRY(sys_device_require_op(owner, port, CONTRACT_UART_OP_SUBSCRIBE, CMD_DIR_WRITE));  // declares the use as well
  ERR_TRY(sys_device_res_add(owner, &sub->res, sub_release, sub));                        // before the port is taken
  ERR_TRY(SYS_DEVICE_SET(port, CONTRACT_UART, OP_SUBSCRIBE, .op = op));
  sub->subscribed = 1;
  return NULL;
}
