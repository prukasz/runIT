#include "sys_ble_router.h"

#include <string.h>

#include "sys_ble.h"
#include "sys_ble_link.h"

/* Changes are staged; APPLY puts them live (with a client connected,
 * CONFIG_SYS_BLE_APPLY_DELAY_MS after its answer; the client reconnects).
 *
 *   key  op           request                                     response
 *   0x01 SVC_CREATE   svc_create_t                                -
 *   0x02 SVC_REMOVE   { u16 uuid }                                -
 *   0x03 CHAR_CREATE  char_create_t, then name (UTF-8, NUL-ended) -
 *   0x04 CHAR_REMOVE  { u16 svc_uuid, u16 uuid }                  -
 *   0x05 APPLY        -                                           -
 *   0x06 STATUS       -                                           status_t
 *   0x07 LIST         -                                           [count u8] then per service:
 *                                                                 { u16 uuid, u8 flags (bit0 primary, bit1 locked), u8 n,
 *                                                                   n x { u16 uuid, u8 flags (bit0 write, bit1 indicate,
 *                                                                   bit2 notify, bit3 framed), u32 tx, u32 rx, name (NUL-ended) } } */

#define OP_SVC_CREATE  0x01u
#define OP_SVC_REMOVE  0x02u
#define OP_CHAR_CREATE 0x03u
#define OP_CHAR_REMOVE 0x04u
#define OP_APPLY       0x05u
#define OP_STATUS      0x06u
#define OP_LIST        0x07u

#define LIST_PRIMARY  (1u << 0)
#define LIST_LOCKED   (1u << 1)
#define LIST_WRITE    (1u << 0)
#define LIST_INDICATE (1u << 1)
#define LIST_NOTIFY   (1u << 2)
#define LIST_FRAMED   (1u << 3)

typedef struct __attribute__((packed)) {
  uint16_t uuid;       /**< Service UUID */
  uint8_t is_primary;  /**< 1 = primary service, 0 = secondary */
} svc_create_t;

typedef struct __attribute__((packed)) {
  uint16_t uuid;  /**< Service UUID */
} svc_remove_t;

typedef struct __attribute__((packed)) {
  uint16_t svc_uuid;        /**< Parent service UUID */
  uint16_t uuid;            /**< Characteristic UUID */
  uint8_t is_write;         /**< 1 = writable, 0 = not writable */
  uint8_t is_indicate;      /**< 1 = indications enabled, 0 = disabled */
  uint8_t is_notify;        /**< 1 = notifications enabled, 0 = disabled */
  uint8_t is_framed;        /**< 1 = framed (fragment header on every packet), 0 = unframed */
  uint32_t tx_buffer_size;  /**< TX buffer size in bytes (0 = no TX) */
  uint32_t rx_buffer_size;  /**< RX buffer size in bytes (0 = no RX) */
} char_create_t;

typedef struct __attribute__((packed)) {
  uint16_t svc_uuid;  /**< Parent service UUID */
  uint16_t uuid;      /**< Characteristic UUID */
} char_remove_t;

typedef struct __attribute__((packed)) {
  uint8_t is_connected;        /**< 1 = connected, 0 = disconnected */
  uint16_t mtu;                /**< Negotiated MTU size in bytes */
  uint32_t rx_overflow_count;  /**< Count of dropped RX items due to full buffer */
} status_t;

ROUTER_OP(op_svc_create, svc_create_t) {
  ERR_CHECK(args->is_primary <= 1, ERR_CMD_BAD_ARG);
  return sys_ble_service_create(&(sys_ble_svc_cfg_t){.uuid = args->uuid, .is_primary = args->is_primary != 0});
}

ROUTER_OP(op_svc_remove, svc_remove_t) {
  ERR_TRY(sys_ble_link_service_remove(args->uuid));
  return sys_ble_service_remove(args->uuid);
}

// A name that is the whole tail: NUL-terminated, nothing after the NUL.
static err_h name_arg(cmd_in_t tail, const char** name) {
  ERR_CHECK(tail.data[tail.len - 1] == '\0' && strlen((const char*)tail.data) == tail.len - 1, ERR_CMD_BAD_ARG);
  *name = (const char*)tail.data;
  return NULL;
}

ROUTER_OP_TAIL(op_char_create, char_create_t) {
  const char* desc;
  ERR_TRY(name_arg(tail, &desc));
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
  ERR_TRY(sys_ble_char_create(args->svc_uuid, &cfg));
  err_h err = sys_ble_link_char_add(args->svc_uuid, &cfg);
  if (err != NULL) {
    err_report(sys_ble_char_remove(args->svc_uuid, args->uuid));
  }
  return err;
}

ROUTER_OP(op_char_remove, char_remove_t) {
  ERR_TRY(sys_ble_link_char_remove(args->uuid));
  return sys_ble_char_remove(args->svc_uuid, args->uuid);
}

ROUTER_OP_NOARGS(op_apply) {
  return sys_ble_apply();
}

ROUTER_OP_NOARGS(op_status) {
  sys_ble_status_t s;
  ERR_TRY(sys_ble_get_status(&s));
  const status_t r = {.is_connected = s.is_connected, .mtu = s.mtu, .rx_overflow_count = s.rx_overflow_count};
  return cmd_reply(out, &r, sizeof(r));
}

typedef struct {
  cmd_out_t* out;
  uint32_t count_at;  // the characteristic count of the service being listed
  uint8_t services;
} list_ctx_t;

static err_h list_one(void* ctx_, const sys_ble_svc_cfg_t* svc, bool locked, const sys_ble_char_cfg_t* chr) {
  list_ctx_t* ctx = ctx_;
  if (chr == NULL) {
    const uint8_t head[4] = {(uint8_t)(svc->uuid & 0xFFu), (uint8_t)(svc->uuid >> 8),
                             (uint8_t)((svc->is_primary ? LIST_PRIMARY : 0u) | (locked ? LIST_LOCKED : 0u)), 0};
    ctx->count_at = ctx->out->len + 3;
    ctx->services++;
    return cmd_append(ctx->out, head, sizeof(head));
  }
  const uint8_t head[3] = {(uint8_t)(chr->uuid & 0xFFu), (uint8_t)(chr->uuid >> 8),
                           (uint8_t)((chr->is_write ? LIST_WRITE : 0u) | (chr->is_indicate ? LIST_INDICATE : 0u) |
                                     (chr->is_notify ? LIST_NOTIFY : 0u) | (chr->is_framed ? LIST_FRAMED : 0u))};
  const uint32_t tx = (uint32_t)chr->tx_buffer_size;
  const uint32_t rx = (uint32_t)chr->rx_buffer_size;
  const char* name = chr->desc != NULL ? chr->desc : "";
  ERR_TRY(cmd_append(ctx->out, head, sizeof(head)));
  ERR_TRY(cmd_append(ctx->out, &tx, sizeof(tx)));  // LE on the ESP32
  ERR_TRY(cmd_append(ctx->out, &rx, sizeof(rx)));
  ERR_TRY(cmd_append(ctx->out, name, (uint32_t)strlen(name) + 1));
  ctx->out->data[ctx->count_at]++;
  return NULL;
}

ROUTER_OP_NOARGS(op_list) {
  const uint8_t count = 0;  // written last
  ERR_TRY(cmd_append(out, &count, sizeof(count)));
  list_ctx_t list = {.out = out};
  ERR_TRY(sys_ble_table_visit(list_one, &list));
  out->data[0] = list.services;
  return NULL;
}

static const router_op_t k_ops[] = {
    ROUTER_W(OP_SVC_CREATE, op_svc_create),
    ROUTER_W(OP_SVC_REMOVE, op_svc_remove),
    ROUTER_W(OP_CHAR_CREATE, op_char_create),
    ROUTER_W(OP_CHAR_REMOVE, op_char_remove),
    ROUTER_W(OP_APPLY, op_apply),
    ROUTER_R(OP_STATUS, op_status),
    ROUTER_R(OP_LIST, op_list),
};

ROUTER_MODULE(sys_ble_router, k_ops)
