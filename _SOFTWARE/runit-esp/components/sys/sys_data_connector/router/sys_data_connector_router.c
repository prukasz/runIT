#include "sys_data_connector_router.h"

#include "sdkconfig.h"
#include "sys_data_connector_internal.h"

typedef struct __attribute__((packed)) {
  uint8_t id;  /**< The connector */
} connector_t;

typedef struct __attribute__((packed)) {
  uint8_t id;                       /**< The connector */
  sys_data_provider_id_t provider;  /**< The provider */
  uint8_t flags;                    /**< SYS_DATA_CONNECTOR_BIND_* */
} bind_t;

typedef struct __attribute__((packed)) {
  uint8_t id;                       /**< The connector */
  sys_data_provider_id_t provider;  /**< The provider */
} unbind_t;

typedef struct __attribute__((packed)) {
  uint8_t id;         /**< The local connector */
  uint8_t board;      /**< The board whose connector it mirrors */
  uint8_t remote_id;  /**< Its connector there; 0 ends the mirror */
} mirror_t;

ROUTER_OP(op_create, sys_data_connector_cfg_t) {
  return sys_data_connector_create(args);
}

ROUTER_OP(op_remove, connector_t) {
  return sys_data_connector_remove(args->id);
}

ROUTER_OP(op_bind, bind_t) {
  return sys_data_connector_bind(args->id, args->provider, args->flags);
}

ROUTER_OP(op_unbind, unbind_t) {
  return sys_data_connector_unbind(args->id, args->provider);
}

ROUTER_OP(op_suspend, connector_t) {
  return sys_data_connector_set_suspended(args->id, true);
}

ROUTER_OP(op_resume, connector_t) {
  return sys_data_connector_set_suspended(args->id, false);
}

// What came in: the message's head, then its bytes, as many as the answer holds.
ROUTER_OP(op_read, connector_t) {
  ERR_CHECK(out->cap >= sizeof(sys_data_connector_msg_t), ERR_CONN_BAD_ARG);
  sys_data_connector_msg_t msg;
  ERR_TRY(sys_data_connector_read(args->id, out->data + sizeof(msg), out->cap - sizeof(msg), &msg));
  memcpy(out->data, &msg, sizeof(msg));
  out->len = sizeof(msg) + msg.len;
  return NULL;
}

ROUTER_OP(op_mirror, mirror_t) {
  return args->remote_id == 0 ? sys_data_connector_unmirror(args->id)
                              : sys_data_connector_mirror(args->id, args->board, args->remote_id, NULL, NULL);
}

ROUTER_OP_NOARGS(op_list) {
  sys_data_connector_info_t list[CONFIG_SYS_DATA_CONNECTOR_MAX];
  const uint8_t count = sys_data_connector_list(list);
  ERR_TRY(cmd_append(out, &count, sizeof(count)));
  for (uint8_t n = 0; n < count; n++) {
    const sys_data_connector_info_t* i = &list[n];
    ERR_TRY(cmd_append(out, &i->head, sizeof(i->head)));
    ERR_TRY(cmd_append(out, i->subs, i->head.subscribers * sizeof(i->subs[0])));
    ERR_TRY(cmd_append(out, i->binds, i->head.bindings * sizeof(i->binds[0])));
  }
  return NULL;
}

// The subscriber is who sent the command: no peer names another.
ROUTER_OP(op_subscribe, connector_t) {
  const err_cmd_t* origin = router_origin();
  ERR_CHECK(origin != NULL, ERR_CONN_BAD_ARG);
  return sys_data_connector_subscribe(args->id, origin->src);
}

ROUTER_OP(op_unsubscribe, connector_t) {
  const err_cmd_t* origin = router_origin();
  ERR_CHECK(origin != NULL, ERR_CONN_BAD_ARG);
  return sys_data_connector_unsubscribe(args->id, origin->src);
}

static const router_op_t k_ops[] = {
    ROUTER_W(SYS_DATA_CONNECTOR_OP_CREATE, op_create),
    ROUTER_W(SYS_DATA_CONNECTOR_OP_REMOVE, op_remove),
    ROUTER_W(SYS_DATA_CONNECTOR_OP_BIND, op_bind),
    ROUTER_W(SYS_DATA_CONNECTOR_OP_UNBIND, op_unbind),
    ROUTER_W(SYS_DATA_CONNECTOR_OP_SUSPEND, op_suspend),
    ROUTER_W(SYS_DATA_CONNECTOR_OP_RESUME, op_resume),
    ROUTER_R(SYS_DATA_CONNECTOR_OP_LIST, op_list),
    ROUTER_R(SYS_DATA_CONNECTOR_OP_READ, op_read),
    ROUTER_W(SYS_DATA_CONNECTOR_OP_SUBSCRIBE, op_subscribe),
    ROUTER_W(SYS_DATA_CONNECTOR_OP_UNSUBSCRIBE, op_unsubscribe),
    ROUTER_W(SYS_DATA_CONNECTOR_OP_MIRROR, op_mirror),
};

ROUTER_MODULE(sys_data_connector_router, k_ops)
