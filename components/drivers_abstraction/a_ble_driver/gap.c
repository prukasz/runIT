#include "gap.h"
#include "common.h"
#include "gatt_svc.h"


static int ble_gap_advertising_start(void);  
static int ble_gap_configure_advertising(void);                          // Starts BLE advertising
static int gap_event_handler(struct ble_gap_event *event, void *arg); // Handles GAP events

/* Private variables */
static uint8_t own_addr_type;
static bool adv_configured = 0;
static uint8_t addr_val[6] = {0};
static uint8_t esp_uri[] = {
    BLE_GAP_URI_PREFIX_HTTPS, 
    't','e','x','t'
};

static int ble_gap_configure_advertising(void){  
    
    const char *name;
    struct ble_hs_adv_fields adv_fields  = {0};  
    struct ble_hs_adv_fields rsp_fields  = {0};

    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;

    name = ble_svc_gap_device_name();  
    adv_fields.name = (uint8_t *)name; //set name 
    adv_fields.name_len = strlen(name);
    adv_fields.name_is_complete = 1;

    adv_fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    adv_fields.tx_pwr_lvl_is_present = 1;
    adv_fields.appearance = BLE_GAP_APPEARANCE_GENERIC_TAG;
    adv_fields.appearance_is_present = 1;
    adv_fields.le_role = BLE_GAP_LE_ROLE_PERIPHERAL;
    adv_fields.le_role_is_present = 1;
    int rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        return rc;
    }
    /**********************/
    rsp_fields.device_addr = addr_val;
    rsp_fields.device_addr_type = own_addr_type;
    rsp_fields.device_addr_is_present = 1;
    rsp_fields.uri = esp_uri;
    rsp_fields.uri_len = sizeof(esp_uri);
        rsp_fields.adv_itvl = BLE_GAP_ADV_ITVL_MS(500);
    rsp_fields.adv_itvl_is_present = 1;
    adv_configured = true;
    return ble_gap_adv_rsp_set_fields(&rsp_fields);

}

static int ble_gap_advertising_start(void) {
    if (!adv_configured){
        ESP_LOGW(TAG, "configureing adv first");
        int rc = ble_gap_configure_advertising();
        if (rc != 0) {
            return rc;
        }
    }

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min  = BLE_GAP_ADV_ITVL_MS(500); 
    adv_params.itvl_max  = BLE_GAP_ADV_ITVL_MS(510);
    return ble_gap_adv_start(own_addr_type, NULL, BLE_HS_FOREVER, &adv_params,
                           gap_event_handler, NULL);
}

static int gap_event_handler(struct ble_gap_event *event, void *arg) {

    struct ble_gap_conn_desc desc; 

    switch (event->type) {  
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {//aka success
            int rc = ble_gap_conn_find(event->connect.conn_handle, &desc);//retrieve details of conncetion into descriptor
            if (rc != 0) {
                return rc;
            }
            struct ble_gap_upd_params params = { //update paremeters
                .itvl_min = 6,   // 7.5 ms  
                .itvl_max = 12,  // 15 ms   
                .latency = 0,   
                .supervision_timeout = 100  // 1 s 
            };
            return ble_gap_update_params(event->connect.conn_handle, &params);
        } 
        else { return ble_gap_advertising_start();} //start adv if fail

    case BLE_GAP_EVENT_DISCONNECT:
        return ble_gap_advertising_start(); //if device disconnect start advertising again
    case BLE_GAP_EVENT_CONN_UPDATE:
        return ble_gap_conn_find(event->conn_update.conn_handle, &desc);
    case BLE_GAP_EVENT_ADV_COMPLETE: //if adv time ended
        return ble_gap_advertising_start();
    case BLE_GAP_EVENT_NOTIFY_TX:  //when notify transmision is finished
        if (event->notify_tx.indication && event->notify_tx.status == BLE_HS_EDONE) { 
        }
        return 0;
    case BLE_GAP_EVENT_SUBSCRIBE: //if subscribed to characteristics cccd
        gatt_svr_subscribe_cb(event);
        return 0;
    case BLE_GAP_EVENT_MTU:
         ESP_LOGI(TAG, "Negotiated MTU: conn_handle=%d mtu=%d",
                 event->mtu.conn_handle, event->mtu.value);
            return 0;
    }
    return 0;
}

/* Public functions */
int ble_gap_advertising_init(void) { // Initializes device address and starts advertising
    int rc = ble_hs_util_ensure_addr(0); //ensures has valid address (bt MAC), if not set genetate
    if (rc != 0) return rc;
    rc = ble_hs_id_infer_auto(0, &own_addr_type); //best addr type for advertising
    if (rc != 0) return rc;
    rc = ble_hs_id_copy_addr(own_addr_type, addr_val, NULL); //copy addres for reuse
    if (rc != 0) return rc;
    rc = ble_gap_configure_advertising();
    if (rc != 0) return rc;
    return ble_gap_advertising_start();
}

int ble_gap_configure() { 
    ble_svc_gap_init();
    ble_att_set_preferred_mtu(BLE_ATT_MTU_MAX);
    return 0;
}

int ble_gap_reconfigure_advertising(void) {
    adv_configured = false;

    if (!ble_hs_synced()) {
        return 0;
    }

    if (ble_gap_adv_active()) {
        int rc = ble_gap_adv_stop();
        if (rc != 0 && rc != BLE_HS_EALREADY) {
            return rc;
        }
    }

    return ble_gap_advertising_init();
}





