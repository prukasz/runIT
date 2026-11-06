#include "sys_board_router.h"
#include "sys_board.h"

ROUTER_OP_TAIL(op_append, sys_board_append_args_t) {
  return sys_nvs_json_append(sys_board_json(), args->offset, tail.data, tail.len);
}

ROUTER_OP_NOARGS(op_erase) {
  return sys_nvs_json_erase(sys_board_json());
}

ROUTER_OP_NOARGS(op_info) {
  const sys_nvs_json_info_t info = sys_nvs_json_info(sys_board_json());
  return cmd_reply(out, &info, sizeof(info));
}

ROUTER_OP(op_read, sys_board_read_args_t) {
  return sys_nvs_json_get(sys_board_json(), args->chunk, out->data, out->cap, &out->len);
}

static const router_op_t k_ops[] = {
    ROUTER_W(SYS_BOARD_OP_JSON_APPEND, op_append),
    ROUTER_W(SYS_BOARD_OP_JSON_ERASE, op_erase),
    ROUTER_R(SYS_BOARD_OP_JSON_INFO, op_info),
    ROUTER_R(SYS_BOARD_OP_JSON_READ, op_read),
};

ROUTER_MODULE(sys_board_router, k_ops)
