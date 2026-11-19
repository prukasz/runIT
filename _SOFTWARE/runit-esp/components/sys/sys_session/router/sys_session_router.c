#include "sys_session_router.h"

#include "sdkconfig.h"
#include "sys_session_internal.h"

ROUTER_READ(SESSION, OP_ID) {
  return CMD_REPLY(SESSION, OP_ID, .board_id = sys_session_get_board_id());
}

ROUTER_WRITE(SESSION, OP_ID) {
  ERR_CHECK_IN_RANGE(args->board_id, 0, CMD_SRC_BOARD_LAST, ERR_CMD_BAD_ARG);
  return sys_session_board_save_id(args->board_id);
}

// PEERS: u8 count, then rows of its value, one per session.
ROUTER_READ(SESSION, OP_PEERS) {
  SESSION_value_OP_PEERS_t list[CONFIG_SYS_SESSION_MAX];
  const uint8_t count = sys_session_list(list);
  ERR_TRY(cmd_append(out, &count, sizeof(count)));
  return cmd_append(out, list, count * sizeof(list[0]));
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY(SESSION, OP_ID),
    ROUTER_ENTRY_R(SESSION, OP_PEERS),
};

ROUTER_MODULE(sys_session_router, k_ops)

ROUTER_MODULE_REGISTER(MODULE_ID_SESSION, sys_session_router)
