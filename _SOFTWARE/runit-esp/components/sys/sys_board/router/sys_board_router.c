#include "sys_board_router.h"

#include "sys_board_internal.h"

ROUTER_WRITE_TAIL(BOARD, OP_JSON_APPEND) {
  return sys_nvs_json_append(sys_board_json(), args->offset, tail, tail_len);
}

ROUTER_WRITE(BOARD, OP_JSON_ERASE) {
  return sys_nvs_json_erase(sys_board_json());
}

ROUTER_READ(BOARD, OP_JSON_INFO) {
  const sys_nvs_json_info_t info = sys_nvs_json_info(sys_board_json());
  return cmd_reply(out, &info, sizeof(info));
}

ROUTER_READ(BOARD, OP_JSON_READ) {
  return sys_nvs_json_get(sys_board_json(), args->chunk, out->data, out->cap, &out->len);
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY_W(BOARD, OP_JSON_APPEND),
    ROUTER_ENTRY_W(BOARD, OP_JSON_ERASE),
    ROUTER_ENTRY_R(BOARD, OP_JSON_INFO),
    ROUTER_ENTRY_R(BOARD, OP_JSON_READ),
};

ROUTER_MODULE(sys_board_router, k_ops)
ROUTER_MODULE_REGISTER(MODULE_ID_BOARD, sys_board_router)
