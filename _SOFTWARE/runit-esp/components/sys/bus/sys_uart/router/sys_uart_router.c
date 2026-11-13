#include "sys_uart_router.h"

#include "sys_uart.h"

ROUTER_OP(op_create, sys_uart_cfg_t) {
  return sys_uart_create(args);
}

ROUTER_OP(op_get, sys_uart_port_args_t) {
  sys_uart_cfg_t cfg;
  ERR_TRY(sys_uart_get(args->port, &cfg));
  return cmd_reply(out, &cfg, sizeof(cfg));
}

static err_h raw(const sys_uart_raw_args_t* args, const uint8_t* tx, uint32_t tx_len, cmd_out_t* out) {
  const uint32_t cap = args->rx_max < out->cap ? args->rx_max : out->cap;
  return sys_uart_transact(args->provider, tx, tx_len, out->data, cap, &out->len, args->wait_ms);
}

ROUTER_OP_TAIL(op_raw, sys_uart_raw_args_t) {
  return raw(args, tail.data, tail.len, out);
}

ROUTER_OP(op_receive, sys_uart_raw_args_t) {
  return raw(args, NULL, 0, out);
}

static const router_op_t k_ops[] = {
    ROUTER_RW(SYS_UART_OP_CREATE, op_get, op_create),
    ROUTER_W(SYS_UART_OP_RAW, op_raw),
    ROUTER_R(SYS_UART_OP_RECEIVE, op_receive),
};

ROUTER_MODULE(sys_uart_router, k_ops)
