#include "sys_ble_router.h"
#include <string.h>
#include "sys_ble.h"

/* Changes are staged; APPLY puts them live (with a client connected ~200 ms
 * after its answer, and the client reconnects).
 *
 *   key  op           request                                     response
 *   0x01 SVC_CREATE   svc_create_t                                -
 *   0x02 SVC_REMOVE   { u16 uuid }                                -
 *   0x03 CHAR_CREATE  char_create_t, then name (UTF-8, NUL-ended) -
 *   0x04 CHAR_REMOVE  { u16 svc_uuid, u16 uuid }                  -
 *   0x05 APPLY        -                                           -
 *   0x06 STATUS       -                                           status_t */

#define OP_SVC_CREATE 0x01u
#define OP_SVC_REMOVE 0x02u
#define OP_CHAR_CREATE 0x03u
#define OP_CHAR_REMOVE 0x04u
#define OP_APPLY 0x05u
#define OP_STATUS 0x06u

typedef struct __attribute__((packed)) {
  uint16_t uuid;
  uint8_t is_primary;
} svc_create_t;

typedef struct __attribute__((packed)) {
  uint16_t uuid;
} svc_remove_t;

typedef struct __attribute__((packed)) {
  uint16_t svc_uuid;
  uint16_t uuid;
  uint8_t is_write;
  uint8_t is_indicate;
  uint8_t is_notify;
  uint8_t is_framed;
  uint32_t tx_buffer_size;  // bytes, 0 = no TX
  uint32_t rx_buffer_size;  // bytes, 0 = no RX buffer
} char_create_t;

typedef struct __attribute__((packed)) {
  uint16_t svc_uuid;
  uint16_t uuid;
} char_remove_t;

typedef struct __attribute__((packed)) {
  uint8_t is_connected;
  uint16_t mtu;
  uint32_t rx_overflow_count;
} status_t;

ROUTER_OP(op_svc_create, svc_create_t) {
  ERR_CHECK(a->is_primary <= 1, ERR_CMD_BAD_ARG);
  return sys_ble_service_create(&(sys_ble_svc_cfg_t){.uuid = a->uuid, .is_primary = a->is_primary != 0});
}

ROUTER_OP(op_svc_remove, svc_remove_t) {
  return sys_ble_service_remove(a->uuid);
}

ROUTER_OP_TAIL(op_char_create, char_create_t) {
  ERR_CHECK(memchr(tail, '\0', tail_len) != NULL, ERR_CMD_BAD_ARG);  // name must be NUL-terminated
  ERR_CHECK(a->is_write <= 1 && a->is_indicate <= 1 && a->is_notify <= 1 && a->is_framed <= 1, ERR_CMD_BAD_ARG);
  const sys_ble_char_cfg_t cfg = {
      .uuid = a->uuid,
      .is_write = a->is_write != 0,
      .is_indicate = a->is_indicate != 0,
      .is_notify = a->is_notify != 0,
      .is_framed = a->is_framed != 0,
      .tx_buffer_size = a->tx_buffer_size,
      .rx_buffer_size = a->rx_buffer_size,
      .desc = (const char*)tail,
  };
  return sys_ble_char_create(a->svc_uuid, &cfg);
}

ROUTER_OP(op_char_remove, char_remove_t) {
  return sys_ble_char_remove(a->svc_uuid, a->uuid);
}

ROUTER_OP_NOARGS(op_apply) {
  return sys_ble_apply();
}

ROUTER_OP_NOARGS(op_status) {
  sys_ble_status_t s;
  ERR_TRY(sys_ble_get_status(&s));
  const status_t r = {.is_connected = s.is_connected, .mtu = s.mtu, .rx_overflow_count = s.rx_overflow_count};
  return cmd_reply(out, out_cap, out_len, &r, sizeof(r));
}

static const router_entry_t k_ops[] = {
    {OP_SVC_CREATE, op_svc_create}, {OP_SVC_REMOVE, op_svc_remove}, {OP_CHAR_CREATE, op_char_create},
    {OP_CHAR_REMOVE, op_char_remove}, {OP_APPLY, op_apply},          {OP_STATUS, op_status},
};

ROUTER_MODULE(sys_ble_router, k_ops)
