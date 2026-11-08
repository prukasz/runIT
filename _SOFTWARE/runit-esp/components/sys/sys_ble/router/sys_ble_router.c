#include "sys_ble_router.h"

#include <stdint.h>
#include <string.h>

#include "sys_ble.h"
#include "sys_ble_central.h"
#include "sys_ble_link.h"
#include "sdkconfig.h"
#include "utils.h"

#define SCAN_MIN_MS 100u
#define SCAN_MAX_MS 30000u

_Static_assert(sizeof(((sys_ble_found_info_t*)0)->name) == SYS_BLE_CENTRAL_NAME_MAX, "a found board's name");

ROUTER_OP(op_svc_create, sys_ble_svc_create_args_t) {
  ERR_CHECK(args->is_primary <= 1, ERR_CMD_BAD_ARG);
  return sys_ble_service_create(&(sys_ble_svc_cfg_t){.uuid = args->uuid, .is_primary = args->is_primary != 0});
}

ROUTER_OP(op_svc_remove, sys_ble_svc_remove_args_t) {
  ERR_TRY(sys_ble_link_service_remove(args->uuid));
  return sys_ble_service_remove(args->uuid);
}

// A name that is the whole tail: NUL-terminated, nothing after the NUL.
static err_h name_arg(cmd_in_t tail, const char** name) {
  ERR_CHECK(tail.data[tail.len - 1] == '\0' && strlen((const char*)tail.data) == tail.len - 1, ERR_CMD_BAD_ARG);
  *name = (const char*)tail.data;
  return NULL;
}

ROUTER_OP_TAIL(op_char_create, sys_ble_char_create_args_t) {
  const char* desc;
  ERR_TRY(name_arg(tail, &desc));
  ERR_CHECK(args->is_write <= 1 && args->is_indicate <= 1 && args->is_notify <= 1 && args->is_framed <= 1 && args->has_provider <= 1, ERR_CMD_BAD_ARG);
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
  err_h err = args->has_provider ? sys_ble_link_char_add(args->svc_uuid, &cfg, args->provider) : NULL;
  if (err != NULL) {
    err_report(sys_ble_char_remove(args->svc_uuid, args->uuid));
  }
  return err;
}

ROUTER_OP(op_char_remove, sys_ble_char_remove_args_t) {
  ERR_TRY(sys_ble_link_char_remove(args->uuid));
  return sys_ble_char_remove(args->svc_uuid, args->uuid);
}

ROUTER_OP_NOARGS(op_apply) {
  return sys_ble_apply();
}

ROUTER_OP_NOARGS(op_status) {
  sys_ble_status_t s;
  ERR_TRY(sys_ble_get_status(&s));
  const sys_ble_status_reply_t r = {.is_connected = s.is_connected, .mtu = s.mtu, .rx_overflow_count = s.rx_overflow_count};
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
    const uint8_t head[4] = {
        (uint8_t)(svc->uuid & 0xFFu), (uint8_t)(svc->uuid >> 8),
        (uint8_t)((svc->is_primary ? SYS_BLE_LIST_PRIMARY : 0u) | (locked ? SYS_BLE_LIST_LOCKED : 0u)), 0};
    ctx->count_at = ctx->out->len + 3;
    ctx->services++;
    return cmd_append(ctx->out, head, sizeof(head));
  }
  uint8_t provider = 0;
  const bool has_provider = sys_ble_link_char_provider(chr->uuid, &provider);
  const uint8_t head[3] = {
      (uint8_t)(chr->uuid & 0xFFu), (uint8_t)(chr->uuid >> 8),
      (uint8_t)((chr->is_write ? SYS_BLE_LIST_WRITE : 0u) | (chr->is_indicate ? SYS_BLE_LIST_INDICATE : 0u) |
                (chr->is_notify ? SYS_BLE_LIST_NOTIFY : 0u) | (chr->is_framed ? SYS_BLE_LIST_FRAMED : 0u) |
                (has_provider ? SYS_BLE_LIST_PROVIDER : 0u))};
  const uint32_t tx = (uint32_t)chr->tx_buffer_size;
  const uint32_t rx = (uint32_t)chr->rx_buffer_size;
  const char* name = chr->desc != NULL ? chr->desc : "";
  ERR_TRY(cmd_append(ctx->out, head, sizeof(head)));
  ERR_TRY(cmd_append(ctx->out, &tx, sizeof(tx)));  // LE on the ESP32
  ERR_TRY(cmd_append(ctx->out, &rx, sizeof(rx)));
  ERR_TRY(cmd_append(ctx->out, &provider, sizeof(provider)));
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

ROUTER_OP(op_scan, sys_ble_scan_args_t) {
  ERR_CHECK(args->duration_ms >= SCAN_MIN_MS && args->duration_ms <= SCAN_MAX_MS, ERR_CMD_BAD_ARG);
  return sys_ble_central_scan(args->duration_ms);
}

ROUTER_OP_NOARGS(op_found) {
  sys_ble_found_t found[CONFIG_SYS_BLE_CENTRAL_FOUND_MAX];
  bool scanning = false;
  const uint8_t n = sys_ble_central_found(found, ARRAY_LEN(found), &scanning);
  const sys_ble_found_head_t head = {.scanning = scanning, .count = n};
  ERR_TRY(cmd_append(out, &head, sizeof(head)));
  for (uint8_t i = 0; i < n; i++) {
    sys_ble_found_info_t info = {.addr_type = found[i].addr_type, .rssi = found[i].rssi};
    memcpy(info.addr, found[i].addr, sizeof(info.addr));
    memcpy(info.name, found[i].name, sizeof(info.name));
    ERR_TRY(cmd_append(out, &info, sizeof(info)));
  }
  return NULL;
}

ROUTER_OP(op_connect, sys_ble_connect_args_t) {
  ERR_CHECK(args->addr_type <= 1, ERR_CMD_BAD_ARG);
  return sys_ble_central_connect(args->provider, args->addr_type, args->addr);
}

static const router_op_t k_ops[] = {
    ROUTER_W(SYS_BLE_OP_SVC_CREATE, op_svc_create),
    ROUTER_W(SYS_BLE_OP_SVC_REMOVE, op_svc_remove),
    ROUTER_W(SYS_BLE_OP_CHAR_CREATE, op_char_create),
    ROUTER_W(SYS_BLE_OP_CHAR_REMOVE, op_char_remove),
    ROUTER_W(SYS_BLE_OP_APPLY, op_apply),
    ROUTER_R(SYS_BLE_OP_STATUS, op_status),
    ROUTER_R(SYS_BLE_OP_LIST, op_list),
    ROUTER_W(SYS_BLE_OP_SCAN, op_scan),
    ROUTER_R(SYS_BLE_OP_FOUND, op_found),
    ROUTER_W(SYS_BLE_OP_CONNECT, op_connect),
};

ROUTER_MODULE(sys_ble_router, k_ops)
