#include "sys_session_router.h"

#include "board_id.h"
#include "sdkconfig.h"
#include "sys_session_internal.h"

ROUTER_READ(SESSION, OP_ID) {
  return CMD_REPLY(SESSION, OP_ID, .board_id = board_id());
}

ROUTER_WRITE(SESSION, OP_ID) {
  return board_id_save(args->board_id);
}

// PEERS: u8 count, then rows of its value, one per session.
ROUTER_READ(SESSION, OP_PEERS) {
  SESSION_value_OP_PEERS_t list[CONFIG_SYS_SESSION_MAX];
  const uint8_t count = sys_session_list(list);
  ERR_TRY(cmd_append(out, &count, sizeof(count)));
  return cmd_append(out, list, count * sizeof(list[0]));
}

// ATTACH: the head, then a u8 module per sub-port.
ROUTER_WRITE_TAIL(SESSION, OP_ATTACH) {
  return sys_session_attach(args->provider, args->deadman_ms, args->flags, tail, tail_len);
}

ROUTER_WRITE(SESSION, OP_DETACH) {
  return sys_session_detach(args->provider);
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY(SESSION, OP_ID),
    ROUTER_ENTRY_R(SESSION, OP_PEERS),
    ROUTER_ENTRY_W(SESSION, OP_ATTACH),
    ROUTER_ENTRY_W(SESSION, OP_DETACH),
};

ROUTER_MODULE(sys_session_router, k_ops)

ROUTER_MODULE_REGISTER(MODULE_ID_SESSION, sys_session_router)
