#pragma once
#include "common.h"
#include "host/ble_gap.h"
#include "services/gap/ble_svc_gap.h"

#define BLE_GAP_APPEARANCE_GENERIC_TAG 0x0200
#define BLE_GAP_URI_PREFIX_HTTPS 0x17
#define BLE_GAP_LE_ROLE_PERIPHERAL 0x00

int ble_gap_advertising_init(void);
int ble_gap_configure(void);
int ble_gap_reconfigure_advertising(void);

// Directly-called event handlers from gap.c
int a_ble_on_connect(struct ble_gap_event *event);
int a_ble_on_disconnect(struct ble_gap_event *event);
int a_ble_on_tx_complete(struct ble_gap_event *event);
int a_ble_on_mtu_update(struct ble_gap_event *event);
