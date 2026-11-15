#include "sys_ble_router.h"

#include <string.h>

#include "sys_ble_internal.h"

ROUTER_WRITE(BLE, OP_SVC_CREATE) {
  ERR_CHECK((args->flags & ~SYS_BLE_SVC_PRIMARY) == 0, ERR_CMD_BAD_ARG);
  return sys_ble_service_create(args->uuid, args->flags != 0);
}

ROUTER_WRITE(BLE, OP_SVC_REMOVE) {
  return sys_ble_service_remove(args->uuid);
}

// The name is the whole tail, NUL-ended; none: no tail.
ROUTER_WRITE_TAIL(BLE, OP_CHAR_CREATE) {
  const uint8_t known = SYS_BLE_CHR_WRITE | SYS_BLE_CHR_INDICATE | SYS_BLE_CHR_NOTIFY | SYS_BLE_CHR_FRAMED;
  ERR_CHECK((args->flags & ~known) == 0, ERR_CMD_BAD_ARG);
  ERR_CHECK(tail_len == 0 || strnlen((const char*)tail, tail_len) == tail_len - 1, ERR_CMD_BAD_ARG);
  return sys_ble_char_create(args->svc, args->uuid, args->flags, args->tx, tail_len > 0 ? (const char*)tail : NULL);
}

ROUTER_WRITE(BLE, OP_CHAR_REMOVE) {
  return sys_ble_char_remove(args->uuid);
}

ROUTER_WRITE(BLE, OP_APPLY) {
  sys_ble_apply();
  return NULL;
}

ROUTER_GETTER(BLE, OP_STATUS, sys_ble_status)

ROUTER_READ(BLE, OP_LIST) {
  return sys_ble_list(out);
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY_W(BLE, OP_SVC_CREATE),  ROUTER_ENTRY_W(BLE, OP_SVC_REMOVE), ROUTER_ENTRY_W(BLE, OP_CHAR_CREATE),
    ROUTER_ENTRY_W(BLE, OP_CHAR_REMOVE), ROUTER_ENTRY_W(BLE, OP_APPLY),      ROUTER_ENTRY_R(BLE, OP_STATUS),
    ROUTER_ENTRY_R(BLE, OP_LIST),
};

ROUTER_MODULE(sys_ble_router, k_ops)
ROUTER_MODULE_REGISTER(BLE, sys_ble_router)
