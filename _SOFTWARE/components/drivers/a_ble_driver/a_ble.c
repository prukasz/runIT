#include "a_ble.h"
#include "gap.h"
#include "gatt_svc.h"
#include "nimble/nimble_port.h"
#include "nvs_flash.h"
#include "rtos_utils.h"


#define TAG  __FILE_NAME__

static RingbufHandle_t a_ble_rx_buffer = NULL;  
static  a_ble_host_cfg_t* host_cfg;
static uint16_t a_ble_mtu_size = 527;

/**
 * @brief Add a ring buffer for received data from BLE
 * @param rb Created ring buffer to store all received data from BLE
 */
void a_ble_add_rx_buffer(RingbufHandle_t rb) {
    a_ble_rx_buffer = rb;
}


static int _ble_on_disconnect(struct ble_gap_event *event) {
    if (event->disconnect.reason != BLE_HS_EDONE) {
        if (event->disconnect.reason >= 0x0200 && event->disconnect.reason <= 0x02FF) {
            ESP_LOGW(TAG, "Disconnected: HCI Reason=0x%02X", (event->disconnect.reason - 0x0200));
        } else {
            ESP_LOGW(TAG, "Disconnected: Host Reason=%d", event->disconnect.reason);
        }
        //Notify that there is no present connection (failed)

        xEventGroupSetBits(host_cfg->event_group, host_cfg->host_bits.bit_on_connection_failed); 
        
    
    }else{
        //notify that there is no present connection (disconnected but not failed)
        xEventGroupSetBits(host_cfg->event_group, host_cfg->host_bits.bit_on_disconnect); 
    }
    //wake up supervisor task to handle this event
    xTaskNotifyGive(host_cfg->supervisor_task_handle);
    return ble_gap_reconfigure_advertising();
}

static int _ble_on_tx_complete(struct ble_gap_event *event) {
    if (event->notify_tx.indication && event->notify_tx.status == BLE_HS_EDONE) {
        // Notify that notification process complete
        R_EVENT_SET(host_cfg->event_group, host_cfg->tx_bits.bit_on_indication_complete);
    } else if (!event->notify_tx.indication && event->notify_tx.status == 0) {
        // Notify that notification process complete
        R_EVENT_SET(host_cfg->event_group, host_cfg->tx_bits.bit_on_notification_complete);
    } else if (event->notify_tx.indication && event->notify_tx.status == BLE_HS_ETIMEOUT) {
        // Notify that indication failed
        R_EVENT_SET(host_cfg->event_group, host_cfg->tx_bits.bit_on_indication_timeout);
    }
    return 0;
}

static int _ble_on_mtu_update(struct ble_gap_event *event) {
    a_ble_mtu_size = event->mtu.value;
    ESP_LOGI(TAG, "MTU updated: conn_handle=%d mtu=%d", event->mtu.conn_handle, event->mtu.value);  
    // Notify that MTU update complete only manager 
    R_EVENT_SET(host_cfg->event_group, host_cfg->host_bits.bit_on_mtu_change);
    return 0;
}


static int _ble_on_connect(struct ble_gap_event *event) {
    if (event->connect.status == 0) {
        ESP_LOGI(TAG, "Connected: conn_handle=%d", event->connect.conn_handle);
        R_EVENT_SET(host_cfg->event_group, host_cfg->host_bits.bit_on_connect);
        xTaskNotifyGive(host_cfg->supervisor_task_handle);
    } else {
        ESP_LOGW(TAG, "Connection failed: status=%d", event->connect.status);
        R_EVENT_SET(host_cfg->event_group, host_cfg->host_bits.bit_on_connection_failed);
        xTaskNotifyGive(host_cfg->supervisor_task_handle);
    }
    return 0;
}



static int _ble_on_rx(struct ble_gatt_access_ctxt *ctxt) {
    size_t len = OS_MBUF_PKTLEN(ctxt->om);
             
    if (len > 0) {
        if (a_ble_rx_buffer == NULL) {
            return 1;
        }
        uint8_t data_buffer[a_ble_mtu_size];
        os_mbuf_copydata(ctxt->om, 0, len, data_buffer);

        if (xRingbufferSend(a_ble_rx_buffer, data_buffer, len, 0) != pdTRUE) {
            R_EVENT_SET(host_cfg->event_group, host_cfg->host_bits.bit_on_rx_failed);
            R_NOTIFY_SEND(host_cfg->supervisor_task_handle, 0);
        } else {
            R_EVENT_SET(host_cfg->event_group, host_cfg->host_bits.bit_on_rx_received);
            R_NOTIFY_SEND(host_cfg->supervisor_task_handle, 0);
        }
    }
    return 0;
}


void ble_store_config_init(void);
static void on_stack_reset(int reason); // Called on BLE stack reset
static void on_stack_sync(void);        // Called when stack syncs with controller
static void nimble_host_config_init(void); // Initialize NimBLE host callbacks
static void nimble_host_task(void *param); // NimBLE host task loop

static void on_stack_reset(int reason){
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

/**
* @brief Set visible BLE name
* @param name - name to appear
* @return ESP_OK on Success
* @return ESP_FAIL on Fail
*/
esp_err_t a_ble_set_name(const char* name){
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

/**
* @brief Start BLE
* @return ESP_OK on Success
*/
esp_err_t a_ble_init(a_ble_host_cfg_t *events_cfg) {
    //store all events
    host_cfg = events_cfg;
    //add callbacks for events
    a_ble_add_callback_on_connect(&_ble_on_connect);
    a_ble_add_callback_on_tx_complete(&_ble_on_tx_complete);
    a_ble_add_callback_on_disconnect(&_ble_on_disconnect);
    a_ble_add_callback_on_mtu_update(&_ble_on_mtu_update);
    a_ble_add_callback_on_write(&_ble_on_rx);

    esp_err_t res;
    res = nvs_flash_init();
    if (res == ESP_ERR_NVS_NO_FREE_PAGES || res == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        res = nvs_flash_init();
    }
    res = nimble_port_init();
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NimBLE host: %d", res);
        return res;
    }
    // Initialize GAP and GATT services
    res = ble_gap_configure();
    if (res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure BLE gap: %d", res);
        return res;
    }
    int gatt_res = gatt_svc_init();
    if (gatt_res != 0) {
        ESP_LOGE(TAG, "Failed to initialize GATT services: %d", gatt_res);
        return gatt_res;
    }

    // Configure NimBLE host callbacks
    nimble_host_config_init();  

    xTaskCreate(nimble_host_task, "BLE", 4*1024, NULL, 3, &host_cfg->manager_task_handle);

    return a_ble_set_name("runit");
}

/**
* @brief Sent notification on chosen characteristic 
* @param conn_handle  connection
* @param chr_val_handle  characteristic
* @param data  data buffer to send
* @param len  of data buffer
* @return ESP_OK on Success, ESP_ERR_NO_MEM where os_buff full, ESP_FAIL on fail
*/
esp_err_t a_ble_send_notification(uint16_t conn_handle, uint16_t chr_val_handle, const uint8_t *data, size_t len) {
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_notify_custom(conn_handle, chr_val_handle, om);

    if (rc != 0) {
        if (rc == BLE_HS_ENOMEM) {
             return ESP_ERR_NO_MEM;
        } else if (rc == BLE_HS_ENOTCONN) {
             return 6; // Return ENOTCONN code gracefully without printing error logs 
        }
        ESP_LOGE(TAG, "Notify error: %i", rc);
        return ESP_FAIL; 
    }
    return ESP_OK;
}


esp_err_t a_ble_send_indication(uint16_t conn_handle, uint16_t chr_val_handle, const uint8_t *data, size_t len) {
    if (data == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (om == NULL) {
        return ESP_ERR_NO_MEM;
    }

    int rc = ble_gatts_indicate_custom(conn_handle, chr_val_handle, om);
    
    if (rc != 0) {
        ESP_LOGE(TAG, "Indicate error: %d", rc);
        if (rc == BLE_HS_ENOMEM) {
             return ESP_ERR_NO_MEM;
        }
        return ESP_FAIL; 
    }
    
    return ESP_OK;
}

uint16_t a_ble_get_mtu(){
    return a_ble_mtu_size;
}






