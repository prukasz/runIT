#include "sys_session_router.h"

#include "sdkconfig.h"
#include "sys_session_internal.h"

typedef struct __attribute__((packed)) {
  uint8_t board_id;
} id_t_;

typedef struct __attribute__((packed)) {
  sys_data_provider_id_t provider;
  uint8_t peer;    /**< sys_session_peer_e */
  uint8_t access;  /**< sys_session_access_e */
} attach_t;

ROUTER_OP_NOARGS(op_id_get) {
  const uint8_t id = sys_session_board_id();
  return cmd_reply(out, &id, sizeof(id));
}

ROUTER_OP(op_id_set, id_t_) {
  return sys_session_set_board_id(args->board_id);
}

ROUTER_OP(op_attach, attach_t) {
  return sys_session_attach(args->provider, args->peer, args->access);
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
