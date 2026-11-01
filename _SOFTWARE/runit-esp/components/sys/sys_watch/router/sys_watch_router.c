#include "sys_watch_router.h"

#include "sdkconfig.h"

typedef struct __attribute__((packed)) {
  uint8_t id;
} watch_ref_t;

ROUTER_OP_TAIL(op_add, sys_watch_head_t) {
  const err_cmd_t* origin = router_origin();
  ERR_CHECK(origin != NULL, ERR_WATCH_BAD_ARG, .id = args->id);
  return sys_watch_add(args, tail, origin->src);
}

ROUTER_OP(op_remove, watch_ref_t) {
  const err_cmd_t* origin = router_origin();
  ERR_CHECK(origin != NULL, ERR_WATCH_NOT_FOUND, .id = args->id);
  return sys_watch_remove(origin->src, args->id);
}

ROUTER_OP_NOARGS(op_list) {
  sys_watch_info_t list[CONFIG_SYS_WATCH_MAX];
  const uint8_t count = sys_watch_list(list, CONFIG_SYS_WATCH_MAX);
  ERR_TRY(cmd_append(out, &count, sizeof(count)));
  return cmd_append(out, list, count * sizeof(list[0]));
}

static const router_op_t k_ops[] = {
    ROUTER_W(SYS_WATCH_OP_ADD, op_add),
    ROUTER_W(SYS_WATCH_OP_REMOVE, op_remove),
    ROUTER_R(SYS_WATCH_OP_LIST, op_list),
};

ROUTER_MODULE(sys_watch_router, k_ops)
