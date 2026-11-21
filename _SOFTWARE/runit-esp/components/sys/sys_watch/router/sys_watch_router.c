#include "sys_watch_router.h"

#include "router_queue.h"
#include "sys_watch_internal.h"

_Static_assert(1u + CONFIG_SYS_WATCH_MAX * sizeof(sys_watch_info_t) <= CMD_DATA_MAX, "LIST must fit one answer");

ROUTER_WRITE_TAIL(WATCH, OP_ADD) {
  const cmd_req_t* origin = router_origin();
  ERR_CHECK(origin != NULL, ERR_BAD_ARG);
  return sys_watch_add(args, (cmd_in_t){tail, tail_len}, origin->src);
}

ROUTER_WRITE(WATCH, OP_REMOVE) {
  const cmd_req_t* origin = router_origin();
  ERR_CHECK(origin != NULL, ERR_WATCH_NOT_FOUND, .id = args->id);
  return sys_watch_remove(origin->src, args->id);
}

ROUTER_READ(WATCH, OP_LIST) {
  sys_watch_info_t list[CONFIG_SYS_WATCH_MAX];
  const uint8_t count = sys_watch_list(list);
  ERR_TRY(cmd_append(out, &count, sizeof(count)));
  return cmd_append(out, list, (uint32_t)count * sizeof(list[0]));
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY_W(WATCH, OP_ADD),
    ROUTER_ENTRY_W(WATCH, OP_REMOVE),
    ROUTER_ENTRY_R(WATCH, OP_LIST),
};

ROUTER_MODULE(sys_watch_router, k_ops)
ROUTER_MODULE_REGISTER(MODULE_ID_WATCH, sys_watch_router)
