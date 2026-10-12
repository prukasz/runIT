#include "sys_ble_router.h"

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
  uint16_t uuid;  //@alias UUID
  uint8_t is_primary;  //@alias Kind @choices 0=Secondary,1=Primary
} svc_create_t;

typedef struct __attribute__((packed)) {
  uint16_t uuid;  //@alias UUID
} svc_remove_t;

typedef struct __attribute__((packed)) {
  uint16_t svc_uuid;  //@alias Service UUID
  uint16_t uuid;  //@alias UUID
  uint8_t is_write;  //@alias Write @description Clients may write it @choices 0=No,1=Yes
  uint8_t is_indicate;  //@alias Indicate @choices 0=No,1=Yes
  uint8_t is_notify;  //@alias Notify @choices 0=No,1=Yes
  uint8_t is_framed;  //@alias Framed @description Items carry the fragment header @choices 0=No,1=Yes
  uint32_t tx_buffer_size;  //@alias TX buffer @description 0: no TX @unit bytes
  uint32_t rx_buffer_size;  //@alias RX buffer @description 0: no RX buffer @unit bytes
} char_create_t;

typedef struct __attribute__((packed)) {
  uint16_t svc_uuid;  //@alias Service UUID
  uint16_t uuid;  //@alias UUID
} char_remove_t;

typedef struct __attribute__((packed)) {
  uint8_t is_connected;  //@alias Connected @choices 0=No,1=Yes
  uint16_t mtu;  //@alias MTU @unit bytes
  uint32_t rx_overflow_count;  //@alias RX overflows @description Items dropped because the RX buffer was full
} status_t;

ROUTER_OP(op_svc_create, svc_create_t) {
  ERR_CHECK(args->is_primary <= 1, ERR_CMD_BAD_ARG);
  return sys_ble_service_create(&(sys_ble_svc_cfg_t){.uuid = args->uuid, .is_primary = args->is_primary != 0});
}

ROUTER_OP(op_svc_remove, svc_remove_t) {
  return sys_ble_service_remove(args->uuid);
}

ROUTER_OP_TAIL(op_char_create, char_create_t) {
  const char* desc;
  ERR_TRY(cmd_name_arg(tail, tail_len, &desc));
  ERR_CHECK(args->is_write <= 1 && args->is_indicate <= 1 && args->is_notify <= 1 && args->is_framed <= 1, ERR_CMD_BAD_ARG);
  const sys_ble_char_cfg_t cfg = {
      .uuid = args->uuid,
      .is_write = args->is_write != 0,
      .is_indicate = args->is_indicate != 0,
      .is_notify = args->is_notify != 0,
      .is_framed = args->is_framed != 0,
      .tx_buffer_size = args->tx_buffer_size,
      .rx_buffer_size = args->rx_buffer_size,
      .desc = desc,
  };
  return sys_ble_char_create(args->svc_uuid, &cfg);
}

ROUTER_OP(op_char_remove, char_remove_t) {
  return sys_ble_char_remove(args->svc_uuid, args->uuid);
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
    //@op OP_SVC_CREATE @alias Create service @description Stages a GATT service (applied by Apply)
    {OP_SVC_CREATE, op_svc_create},
    //@op OP_SVC_REMOVE @alias Remove service @description Stages the removal of a service
    {OP_SVC_REMOVE, op_svc_remove},
    //@op OP_CHAR_CREATE @alias Create characteristic @description Stages a characteristic of a service @tail string @tail-alias Name @tail-description Shown to clients as the characteristic's description
    {OP_CHAR_CREATE, op_char_create},
    //@op OP_CHAR_REMOVE @alias Remove characteristic @description Stages the removal of a characteristic
    {OP_CHAR_REMOVE, op_char_remove},
    //@op OP_APPLY @alias Apply changes @description Puts the staged GATT table live (a connected client is disconnected and reconnects)
    {OP_APPLY, op_apply},
    //@op OP_STATUS @alias Status @description The connection state @returns status_t
    {OP_STATUS, op_status},
};

ROUTER_MODULE(sys_ble_router, k_ops)
