#pragma once
#include "host/ble_gatt.h"
#include "host/ble_gap.h"

int gatt_svc_init(const struct ble_gatt_svc_def *svcs);
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
void gatt_svr_subscribe_cb(struct ble_gap_event *event);
void gatt_reset_conn_handle(void);
