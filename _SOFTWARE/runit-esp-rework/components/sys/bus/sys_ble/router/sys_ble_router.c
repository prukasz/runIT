#include "sys_ble_router.h"

#include "sys_ble_internal.h"

ROUTER_GETTER(BLE, OP_STATUS, sys_ble_status)

ROUTER_READ(BLE, OP_LIST) {
  return sys_ble_list(out);
}

static const router_op_t k_ops[] = {
    ROUTER_ENTRY_R(BLE, OP_STATUS),
    ROUTER_ENTRY_R(BLE, OP_LIST),
};

ROUTER_MODULE(sys_ble_router, k_ops)
ROUTER_MODULE_REGISTER(MODULE_ID_BLE, sys_ble_router)
