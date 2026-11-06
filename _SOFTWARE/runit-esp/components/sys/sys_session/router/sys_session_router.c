#include "sys_session_router.h"

#include "sdkconfig.h"
#include "sys_session_internal.h"

ROUTER_OP_NOARGS(op_id_get) {
  const sys_session_id_args_t id = {.board_id = sys_session_get_board_id()};
  return cmd_reply(out, &id, sizeof(id));
}

ROUTER_OP(op_id_set, sys_session_id_args_t) {
  return sys_session_set_board_id(args->board_id);
}

ROUTER_OP(op_attach, sys_session_attach_args_t) {
  return sys_session_attach(args->provider, args->peer);
}

ROUTER_OP(op_detach, sys_data_provider_id_t) {
  return sys_session_detach(*args);
}

ROUTER_OP_NOARGS(op_peers) {
  sys_session_info_t list[CONFIG_SYS_SESSION_MAX];
  const uint8_t count = sys_session_list(list);
  ERR_TRY(cmd_append(out, &count, sizeof(count)));
  return cmd_append(out, list, count * sizeof(list[0]));
}

static const router_op_t k_ops[] = {
    ROUTER_RW(SYS_SESSION_OP_ID, op_id_get, op_id_set),
    ROUTER_W(SYS_SESSION_OP_ATTACH, op_attach),
    ROUTER_W(SYS_SESSION_OP_DETACH, op_detach),
    ROUTER_R(SYS_SESSION_OP_PEERS, op_peers),
};

ROUTER_MODULE(sys_session_router, k_ops)
