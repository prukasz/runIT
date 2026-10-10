#include "sys_connector_router.h"
#include <string.h>
#include "sdkconfig.h"
#include "sys_connector.h"

/*   key  op        request                                response
 *   0x01 CREATE    { u8 id, u8 stream }, name (NUL-ended)  -
 *   0x02 REMOVE    { u8 id }                              -
 *   0x03 BIND      { u8 id, u8 provider, u32 endpoint }   -
 *   0x04 UNBIND    { u8 id, u8 provider }                 -
 *   0x05 SUSPEND   { u8 id }                              -
 *   0x06 RESUME    { u8 id }                              -
 *   0x07 INFO      -                                      [count u8] then per connector:
 *                                                         { u8 id, u8 stream, u8 flags (bit0 system, bit1 suspended),
 *                                                           u8 n, n × { u8 provider, u32 endpoint }, name (NUL-ended) } */

#define OP_CREATE 0x01u
#define OP_REMOVE 0x02u
#define OP_BIND 0x03u
#define OP_UNBIND 0x04u
#define OP_SUSPEND 0x05u
#define OP_RESUME 0x06u
#define OP_INFO 0x07u

#define INFO_SYSTEM (1u << 0)
#define INFO_SUSPENDED (1u << 1)

typedef struct __attribute__((packed)) {
  uint8_t id;
  uint8_t stream;
} create_t;

typedef struct __attribute__((packed)) {
  uint8_t id;
} id_t_;

typedef struct __attribute__((packed)) {
  uint8_t id;
  uint8_t provider;
  uint32_t endpoint;
} bind_t;

typedef struct __attribute__((packed)) {
  uint8_t id;
  uint8_t provider;
} unbind_t;

ROUTER_OP_TAIL(op_create, create_t) {
  ERR_CHECK(memchr(tail, '\0', tail_len) != NULL, ERR_CMD_BAD_ARG);  // name must be NUL-terminated
  return sys_connector_create(a->id, a->stream, (const char*)tail);
}

ROUTER_OP(op_remove, id_t_) {
  return sys_connector_remove(a->id);
}

ROUTER_OP(op_bind, bind_t) {
  return sys_connector_bind(a->id, a->provider, a->endpoint);
}

ROUTER_OP(op_unbind, unbind_t) {
  return sys_connector_unbind(a->id, a->provider);
}

ROUTER_OP(op_suspend, id_t_) {
  return sys_connector_suspend(a->id);
}

ROUTER_OP(op_resume, id_t_) {
  return sys_connector_resume(a->id);
}

// Appends `len` bytes to out at *pos; ERR_CMD_OUT_FULL when they don't fit.
static err_h put(uint8_t* out, uint32_t cap, uint32_t* pos, const void* data, uint32_t len) {
  ERR_CHECK(*pos + len <= cap, ERR_CMD_OUT_FULL, .need = (uint16_t)(*pos + len), .cap = (uint16_t)cap);
  memcpy(out + *pos, data, len);
  *pos += len;
  return NULL;
}

ROUTER_OP_NOARGS(op_info) {
  uint32_t pos = 1;  // out[0] = count, written last
  ERR_CHECK(out_cap > 0, ERR_CMD_OUT_FULL, .need = 1, .cap = 0);
  uint8_t count = 0;
  for (uint8_t id = 0; id < CONFIG_SYS_CONNECTOR_MAX; id++) {
    sys_connector_info_t i;
    err_h err = sys_connector_info(id, &i);
    if (err != NULL) {
      err_free(err);  // slot not used
      continue;
    }
    const uint8_t head[4] = {i.id, i.stream,
                             (uint8_t)((i.system ? INFO_SYSTEM : 0) | (i.suspended ? INFO_SUSPENDED : 0)),
                             i.binding_count};
    ERR_TRY(put(out, out_cap, &pos, head, sizeof(head)));
    for (uint8_t b = 0; b < i.binding_count; b++) {
      ERR_TRY(put(out, out_cap, &pos, &i.bindings[b].provider, 1));
      ERR_TRY(put(out, out_cap, &pos, &i.bindings[b].endpoint, sizeof(uint32_t)));  // LE on the ESP32
    }
    ERR_TRY(put(out, out_cap, &pos, i.name, (uint32_t)strlen(i.name) + 1));
    count++;
  }
  out[0] = count;
  *out_len = pos;
  return NULL;
}

static const router_entry_t k_ops[] = {
    {OP_CREATE, op_create},   {OP_REMOVE, op_remove}, {OP_BIND, op_bind}, {OP_UNBIND, op_unbind},
    {OP_SUSPEND, op_suspend}, {OP_RESUME, op_resume}, {OP_INFO, op_info},
};

ROUTER_MODULE(sys_connector_router, k_ops)
