#include "sys_connector_router.h"

#include "sdkconfig.h"
#include "sys_connector_internal.h"

/*   key  op        request                                       response
 *   0x01 CREATE    { u8 connector }                              -
 *   0x02 REMOVE    { u8 connector }                              -
 *   0x03 BIND      { u8 connector, u8 provider, u32 endpoint }   -
 *   0x04 UNBIND    { u8 connector, u8 provider }                 -
 *   0x05 SUSPEND   { u8 connector }                              -
 *   0x06 RESUME    { u8 connector }                              -
 *   0x07 INFO      -                                             [count u8] then per connector:
 *                                                                { u8 connector, u8 flags (bit0 system, bit1 suspended),
 *                                                                  u8 n, n × { u8 provider, u32 endpoint } }
 * A connector is its stream byte: the first byte of every frame it sends. */

#define OP_CREATE  0x01u
#define OP_REMOVE  0x02u
#define OP_BIND    0x03u
#define OP_UNBIND  0x04u
#define OP_SUSPEND 0x05u
#define OP_RESUME  0x06u
#define OP_INFO    0x07u

#define INFO_SYSTEM    (1u << 0)
#define INFO_SUSPENDED (1u << 1)

typedef struct __attribute__((packed)) {
  uint8_t connector;  /**< The connector (its stream byte) */
} connector_t;

typedef struct __attribute__((packed)) {
  uint8_t connector;  /**< The connector (its stream byte) */
  uint8_t provider;   /**< transport_e */
  uint32_t endpoint;  /**< The provider's address */
} bind_t;

typedef struct __attribute__((packed)) {
  uint8_t connector;  /**< The connector (its stream byte) */
  uint8_t provider;   /**< transport_e */
} unbind_t;

ROUTER_OP(op_create, connector_t) {
  return sys_connector_create(args->connector);
}

ROUTER_OP(op_remove, connector_t) {
  return sys_connector_remove(args->connector);
}

ROUTER_OP(op_bind, bind_t) {
  return sys_connector_bind(args->connector, args->provider, args->endpoint);
}

ROUTER_OP(op_unbind, unbind_t) {
  return sys_connector_unbind(args->connector, args->provider);
}

ROUTER_OP(op_suspend, connector_t) {
  return sys_connector_set_suspended(args->connector, true);
}

ROUTER_OP(op_resume, connector_t) {
  return sys_connector_set_suspended(args->connector, false);
}

ROUTER_OP_NOARGS(op_info) {
  uint32_t pos = 1;  // out[0] = count, written last
  ERR_CHECK(out_cap > 0, ERR_CMD_OUT_FULL, .need = 1, .cap = 0);
  sys_connector_info_t list[CONFIG_SYS_CONNECTOR_MAX];
  uint8_t count = sys_connector_list(list);
  for (uint8_t n = 0; n < count; n++) {
    const sys_connector_info_t* i = &list[n];
    const uint8_t head[3] = {i->connector, (uint8_t)((i->system ? INFO_SYSTEM : 0) | (i->suspended ? INFO_SUSPENDED : 0)),
                             i->binding_count};
    ERR_TRY(cmd_append(out, out_cap, &pos, head, sizeof(head)));
    for (uint8_t b = 0; b < i->binding_count; b++) {
      ERR_TRY(cmd_append(out, out_cap, &pos, &i->bindings[b].provider, 1));
      ERR_TRY(cmd_append(out, out_cap, &pos, &i->bindings[b].endpoint, sizeof(uint32_t)));  // LE on the ESP32
    }
  }
  out[0] = count;
  *out_len = pos;
  return NULL;
}

static const router_op_t k_ops[] = {
    ROUTER_W(OP_CREATE, op_create),
    ROUTER_W(OP_REMOVE, op_remove),
    ROUTER_W(OP_BIND, op_bind),
    ROUTER_W(OP_UNBIND, op_unbind),
    ROUTER_W(OP_SUSPEND, op_suspend),
    ROUTER_W(OP_RESUME, op_resume),
    ROUTER_R(OP_INFO, op_info),
};

ROUTER_MODULE(sys_connector_router, k_ops)
