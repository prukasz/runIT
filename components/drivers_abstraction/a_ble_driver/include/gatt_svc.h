#pragma once
#include "host/ble_gatt.h"
#include "services/gatt/ble_svc_gatt.h"
#include "host/ble_gap.h"

int gatt_svc_init();
void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg);
void gatt_svr_subscribe_cb(struct ble_gap_event *event);
                       
typedef struct{         
    bool ind_status;
    bool chr_conn_handle_status;
}indicate_status_t;

typedef struct{         
    bool notify_status;
    bool chr_conn_handle_status;
}notify_status_t;

void send_indication();
void chr_send_indication(indicate_status_t *indicate_status, int16_t chr_conn_handle, int16_t chr_val_handle);
int gatt_send_notify(const uint8_t *data, size_t len);
uint16_t gatt_get_vm_out_conn_handle(void);
uint16_t gatt_get_vm_out_val_handle(void);
bool gatt_can_send_vm_out_indication(void);
void gatt_notify_ready(void);

void gatt_svc_add_callback_on_write(esp_err_t (*callback)(const uint8_t* data, size_t len));


