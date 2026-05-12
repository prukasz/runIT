#pragma once
#include "common.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"

#define BLE_GAP_APPEARANCE_GENERIC_TAG 0x0200
#define BLE_GAP_URI_PREFIX_HTTPS 0x17
#define BLE_GAP_LE_ROLE_PERIPHERAL 0x00

typedef int (*ble_cb_t)(struct ble_gap_event *event);

int ble_gap_advertising_init(void);
int ble_gap_configure(void);
int ble_gap_reconfigure_advertising(void);

void a_ble_add_callback_on_tx_complete(ble_cb_t callback);
void a_ble_add_callback_on_connect(ble_cb_t callback);
void a_ble_add_callback_on_disconnect(ble_cb_t callback);
void a_ble_add_callback_on_mtu_update(ble_cb_t callback);
