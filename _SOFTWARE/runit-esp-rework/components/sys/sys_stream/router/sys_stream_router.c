#include "sys_stream_router.h"

#include "router_queue.h"
#include "sdkconfig.h"
#include "sys_stream_internal.h"

ROUTER_WRITE(STREAM, OP_CREATE) {
  return sys_stream_create(args);
}

ROUTER_WRITE(STREAM, OP_REMOVE) {
  return sys_stream_remove(args->id);
}

ROUTER_WRITE(STREAM, OP_SOURCE) {
  return sys_stream_source(args->id, args->provider);
}

ROUTER_WRITE(STREAM, OP_SINK) {
  return sys_stream_sink(args->id, args->board, args->provider);
}

ROUTER_WRITE(STREAM, OP_UNBIND) {
  return sys_stream_unbind(args->id, args->provider);
}

ROUTER_WRITE(STREAM, OP_UNBIND_ALL) {
  return sys_stream_unbind_all(args->id);
}

ROUTER_READ(STREAM, OP_LIST) {
  sys_stream_info_t list[CONFIG_SYS_STREAM_MAX];
  const uint8_t count = sys_stream_list(list);
  ERR_TRY(cmd_append(out, &count, sizeof(count)));
  for (uint8_t n = 0; n < count; n++) {
    const sys_stream_info_t* i = &list[n];
    ERR_TRY(cmd_append(out, &i->head, sizeof(i->head)));
    ERR_TRY(cmd_append(out, i->targets, (uint32_t)i->head.targets * sizeof(i->targets[0])));
  }
  return NULL;
}

// The subscriber is who sent the command: no peer names another.
ROUTER_WRITE(STREAM, OP_SUBSCRIBE) {
  const cmd_req_t* origin = router_origin();
  ERR_CHECK(origin != NULL, ERR_STREAM_BAD_ARG);
  return sys_stream_subscribe(args->id, origin->src);
}

ROUTER_WRITE(STREAM, OP_UNSUBSCRIBE) {
  const cmd_req_t* origin = router_origin();
  ERR_CHECK(origin != NULL, ERR_STREAM_BAD_ARG);
  return sys_stream_unsubscribe(args->id, origin->src);
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY_W(STREAM, OP_CREATE),      ROUTER_ENTRY_W(STREAM, OP_REMOVE),
    ROUTER_ENTRY_W(STREAM, OP_SOURCE),      ROUTER_ENTRY_W(STREAM, OP_SINK),
    ROUTER_ENTRY_W(STREAM, OP_UNBIND),      ROUTER_ENTRY_W(STREAM, OP_UNBIND_ALL),
    ROUTER_ENTRY_R(STREAM, OP_LIST),        ROUTER_ENTRY_W(STREAM, OP_SUBSCRIBE),
    ROUTER_ENTRY_W(STREAM, OP_UNSUBSCRIBE),
};

ROUTER_MODULE(sys_stream_router, k_ops)
ROUTER_MODULE_REGISTER(STREAM, sys_stream_router)
