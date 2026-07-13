#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/ringbuf.h>

struct ble_gatt_svc_def;

typedef void (*a_ble_connect_fn)(uint16_t conn_handle);
typedef void (*a_ble_disconnect_fn)(uint16_t conn_handle, int reason);
typedef void (*a_ble_failure_fn)(esp_err_t error);

typedef struct {
    TaskHandle_t nimble_port_task_handle; // Renamed from manager_task_handle
    
    // Callbacks to notify sys_ble
    a_ble_connect_fn on_connect;
    a_ble_disconnect_fn on_disconnect;
    a_ble_failure_fn on_failure;

    // Internal TX bits for sending task control
    EventGroupHandle_t tx_event_group;
    EventBits_t bit_tx_indication_complete;
    EventBits_t bit_tx_indication_timeout;
    EventBits_t bit_tx_notification_complete;
} a_ble_host_cfg_t;

esp_err_t a_ble_init(a_ble_host_cfg_t *events_cfg, const struct ble_gatt_svc_def *svcs);
esp_err_t a_ble_set_name(const char* name);
esp_err_t a_ble_send(uint16_t conn_handle, uint16_t chr_val_handle, const uint8_t *data, size_t len, bool indicate);
uint16_t a_ble_get_tx_conn_handle(void);
uint16_t a_ble_get_tx_val_handle(void);
uint16_t a_ble_get_mtu(void);