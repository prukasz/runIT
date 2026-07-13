#include "a_ble.h"
#include "gap.h"
#include "gatt_svc.h"
#include "nimble/nimble_port.h"
#include "nvs_flash.h"

#define TAG __FILE_NAME__

// Private context encapsulating all driver state
typedef struct {
    a_ble_host_cfg_t host_cfg;
    uint16_t mtu_size;
} a_ble_driver_ctx_t;

static a_ble_driver_ctx_t g_drv_ctx = {
    .mtu_size = 527
};

int a_ble_on_disconnect(struct ble_gap_event *event) {
    int reason = event->disconnect.reason;

    if (reason >= 0x0200 && reason <= 0x02FF) {
        ESP_LOGI(TAG, "Disconnected: HCI Reason=0x%02X", (reason - 0x0200));
    } else {
        ESP_LOGI(TAG, "Disconnected: Host Reason=%d", reason);
    }

    if (reason == BLE_HS_EDONE || reason == 0x0213 || reason == 0x0216) {
        if (g_drv_ctx.host_cfg.on_disconnect) {
            g_drv_ctx.host_cfg.on_disconnect(event->disconnect.conn.conn_handle, reason);
        }
    } else {
        ESP_LOGW(TAG, "Connection failed or dropped unexpectedly!");
        if (g_drv_ctx.host_cfg.on_failure) {
            g_drv_ctx.host_cfg.on_failure(ESP_ERR_INVALID_STATE);
        }
    }
    ble_gap_reconfigure_advertising();
    return 0;
}

int a_ble_on_tx_complete(struct ble_gap_event *event) {
    if (event->notify_tx.indication && event->notify_tx.status == BLE_HS_EDONE) {
        xEventGroupSetBits(g_drv_ctx.host_cfg.tx_event_group, g_drv_ctx.host_cfg.bit_tx_indication_complete);
    } else if (!event->notify_tx.indication && event->notify_tx.status == 0) {
        xEventGroupSetBits(g_drv_ctx.host_cfg.tx_event_group, g_drv_ctx.host_cfg.bit_tx_notification_complete);
    } else if (event->notify_tx.indication && event->notify_tx.status == BLE_HS_ETIMEOUT) {
        xEventGroupSetBits(g_drv_ctx.host_cfg.tx_event_group, g_drv_ctx.host_cfg.bit_tx_indication_timeout);
    }
    return 0;
}

int a_ble_on_mtu_update(struct ble_gap_event *event) {
    g_drv_ctx.mtu_size = event->mtu.value;
    ESP_LOGI(TAG, "MTU updated: conn_handle=%d mtu=%d", event->mtu.conn_handle, event->mtu.value);  
    return 0;
}

int a_ble_on_connect(struct ble_gap_event *event) {
    if (event->connect.status == 0) {
        ESP_LOGI(TAG, "Connected: conn_handle=%d", event->connect.conn_handle);
        if (g_drv_ctx.host_cfg.on_connect) {
            g_drv_ctx.host_cfg.on_connect(event->connect.conn_handle);
        }
    } else {
        ESP_LOGW(TAG, "Connection failed: status=%d", event->connect.status);
        if (g_drv_ctx.host_cfg.on_failure) {
            g_drv_ctx.host_cfg.on_failure(ESP_FAIL);
        }
    }
    return 0;
}

void ble_store_config_init(void);
static void on_stack_reset(int reason) {
    ESP_LOGW(TAG, "stack reset reason: %d", reason);
}

static void on_stack_sync(void) {
    ble_gap_advertising_init();
}

static void nimble_host_config_init(void) {
    ble_hs_cfg.reset_cb          = on_stack_reset;
    ble_hs_cfg.sync_cb           = on_stack_sync;
    ble_hs_cfg.gatts_register_cb = gatt_svr_register_cb;
    ble_hs_cfg.store_status_cb   = ble_store_util_status_rr;
    ble_store_config_init();
}

static void nimble_host_task(void *param) {
    (void)param;
    nimble_port_run();
    vTaskDelete(NULL);
}

esp_err_t a_ble_set_name(const char* name) {
    int res = ble_svc_gap_device_name_set(name);
    if (res == 0) {
        int adv_res = ble_gap_reconfigure_advertising();
        if (adv_res != 0) {
            ESP_LOGW(TAG, "Device name set but adv reconfigure failed: %d", adv_res);
        }
        ESP_LOGI(TAG, "Name set to %s", name);
        return ESP_OK;
    }
    ESP_LOGE(TAG, "Failed to set BLE device name, res=%d", res);
    return ESP_FAIL;
}

esp_err_t a_ble_init(a_ble_host_cfg_t *events_cfg, const struct ble_gatt_svc_def *svcs) {
    g_drv_ctx.host_cfg = *events_cfg;

    esp_err_t res = nvs_flash_init();
    if (res == ESP_ERR_NVS_NO_FREE_PAGES || res == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        res = nvs_flash_init();
    }
    res = nimble_port_init();
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NimBLE host: %d", res);
        return res;
    }
    res = ble_gap_configure();
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure BLE gap: %d", res);
        return res;
    }
    int gatt_res = gatt_svc_init(svcs);
    if (gatt_res != 0) {
        ESP_LOGE(TAG, "Failed to initialize GATT services: %d", gatt_res);
        return gatt_res;
    }

    nimble_host_config_init();  

    xTaskCreate(
        nimble_host_task, 
        "BLE_PORT", 
        4*1024, 
        NULL, 
        3, 
        &g_drv_ctx.host_cfg.nimble_port_task_handle
    );

    // Copy back the created task handle
    events_cfg->nimble_port_task_handle = g_drv_ctx.host_cfg.nimble_port_task_handle;

    return a_ble_set_name("runit");
}

esp_err_t a_ble_send(uint16_t conn_handle, uint16_t chr_val_handle, const uint8_t *data, size_t len, bool indicate) {
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int rc;
    if (indicate) {
        rc = ble_gatts_indicate_custom(conn_handle, chr_val_handle, om);
    } else {
        rc = ble_gatts_notify_custom(conn_handle, chr_val_handle, om);
    }

    if (rc != 0) {
        if (indicate) {
            ESP_LOGE(TAG, "Indicate error: %d", rc);
        } else {
            ESP_LOGE(TAG, "Notify error: %d", rc);
        }
        if (rc == BLE_HS_ENOMEM) {
             return ESP_ERR_NO_MEM;
        } else if (rc == BLE_HS_ENOTCONN) {
             return ESP_ERR_INVALID_STATE;
        }
        return ESP_FAIL; 
    }
    return ESP_OK;
}

uint16_t a_ble_get_mtu(void) {
    return g_drv_ctx.mtu_size;
}
