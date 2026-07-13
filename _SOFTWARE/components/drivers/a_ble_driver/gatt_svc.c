#include "gatt_svc.h"
#include "common.h"
#include "services/gatt/ble_svc_gatt.h"
#include <esp_log.h>
#include <string.h>

#define TAG __FILE_NAME__

// External sys_ble subscription handler
extern void sys_ble_on_subscribe(uint16_t conn_handle, uint16_t attr_handle, bool indicate, bool notify);

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    switch (ctxt->op) {
        case BLE_GATT_REGISTER_OP_SVC:
            ESP_LOGI(TAG, "Registered service %s with handle=%d",
                     ble_uuid_to_str(ctxt->svc.svc_def->uuid, (char[80]){0}),
                     ctxt->svc.handle);
            break;
        case BLE_GATT_REGISTER_OP_CHR:
            ESP_LOGI(TAG, "Registered characteristic %s with def_handle=%d val_handle=%d",
                     ble_uuid_to_str(ctxt->chr.chr_def->uuid, (char[80]){0}),
                     ctxt->chr.def_handle,
                     ctxt->chr.val_handle);
            break;
        case BLE_GATT_REGISTER_OP_DSC:
            ESP_LOGI(TAG, "Registered descriptor %s with handle=%d",
                     ble_uuid_to_str(ctxt->dsc.dsc_def->uuid, (char[80]){0}),
                     ctxt->dsc.handle);
            break;
        default:
            break;
    }
}

void gatt_svr_subscribe_cb(struct ble_gap_event *event) {
    sys_ble_on_subscribe(event->subscribe.conn_handle, event->subscribe.attr_handle,
                         event->subscribe.cur_indicate, event->subscribe.cur_notify);
}

int gatt_svc_init(const struct ble_gatt_svc_def *svcs) {
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(svcs);
    if (rc != 0) return rc;
    rc = ble_gatts_add_svcs(svcs);
    if (rc != 0) return rc;
    return 0;
}

void gatt_reset_conn_handle(void) {
    // Stub
}
