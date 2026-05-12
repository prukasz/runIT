#include "gatt_svc.h"
#include "gatt_uuids.h"

#define TAG __FILE_NAME__

// Static pointer to the user-provided callback
static esp_err_t (*_ble_rx_callback)(struct ble_gatt_access_ctxt *ctxt) = NULL;

void a_ble_add_callback_on_write(esp_err_t (*callback)(struct ble_gatt_access_ctxt *ctxt)) {
    _ble_rx_callback = callback;
}   

/*-----callback when characteristics is accesed----*/
static int chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);

/*-----callback when characteristics descriptor is accesed----*/
static int chr_desc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);

/*----Characteristic value handle----*/
static uint16_t chr_val_handle_tx;
static uint16_t chr_val_handle_rx;
  
/*----Characteristic descriptor value handle----*/
static uint16_t chr_desc_val_handle_tx;
static uint16_t chr_desc_val_handle_rx;

/*----Characteristics connection handle----
Represents what device is accesing characteristics*/
static uint16_t chr_conn_handle_tx = 0;
static uint16_t chr_conn_handle_rx = 0;

/*User (readable) descriptor value as text do be displayed*/
static const char chr_desc_tx[] = "TX";
static const char chr_desc_rx[]  = "RX";


/*Indication status struct
Indication is like notification that requires confirmation of being receivied by peer*/
static indicate_status_t indicate_status_tx = {.ind_status = 0, .chr_conn_handle_status = 0};
static notify_status_t notify_status_rx  = {.notify_status = 0, .chr_conn_handle_status = 0};


/*This is main struct for definition of all services and characteristics used by NIMBLE*/
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &uuid_svc_runit.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &uuid_chr_tx.u,
                .access_cb = chr_access_cb,
                .flags = BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
                .val_handle = &chr_val_handle_tx,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .access_cb = chr_desc_access_cb,
                        .att_flags = BLE_ATT_F_READ,
                        .arg = (void*)chr_desc_tx,
                    },
                    {0}
                }
            },
            {
                .uuid = &uuid_chr_rx.u,
                .access_cb = chr_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &chr_val_handle_rx,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .access_cb = chr_desc_access_cb,
                        .att_flags = BLE_ATT_F_READ,
                        .arg = (void*)chr_desc_rx,
                    },
                    {0}
                }
            },
            {0}
        }
    },
    {0}
};

/*-----callback when characteristics is accesed----*/
static int chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    if (attr_handle == chr_val_handle_rx){
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            _ble_rx_callback(ctxt); 
            return 0;
        }
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

/*-----callback when characteristics descriptor is accesed----*/
static int chr_desc_access_cb(uint16_t conn_handle  , uint16_t attr_handle,
                                     struct ble_gatt_access_ctxt *ctxt, void *arg)

{
    const char *chr_user_desc = (char*)arg; //passed text value
    ESP_LOGI(TAG, "%s", chr_user_desc);
    return os_mbuf_append(ctxt->om, chr_user_desc, strlen(chr_user_desc)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES; //copy descriptor to buffer

}

/*Indication wrapper (checks if possible)*/
void chr_send_indication(indicate_status_t *indicate_status, int16_t chr_conn_handle, int16_t chr_val_handle) {
    if (indicate_status->chr_conn_handle_status && indicate_status->ind_status) {
        ble_gatts_indicate(chr_conn_handle, chr_val_handle);
    }
}

void gatt_svr_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg) {
    switch (ctxt->op) {
        case BLE_GATT_REGISTER_OP_CHR:
            ESP_LOGI(TAG, "Characteristic registered: handle=0x%04x", ctxt->chr.val_handle);
            break;
        case BLE_GATT_REGISTER_OP_DSC:
            ESP_LOGI(TAG, "Descriptor registered: handle=0x%04x", ctxt->dsc.handle);
            // Match descriptor by parent characteristic pointer
            if (ctxt->dsc.dsc_def->uuid->type == BLE_UUID_TYPE_16 &&
                ble_uuid_u16(ctxt->dsc.dsc_def->uuid) == 0x2901) { // User description
                if (ctxt->dsc.chr_def->val_handle == &chr_val_handle_tx) {
                    chr_desc_val_handle_tx = ctxt->dsc.handle;
                } else if (ctxt->dsc.chr_def->val_handle == &chr_val_handle_rx) {
                    chr_desc_val_handle_rx = ctxt->dsc.handle;
                }   
            }
            break;
        default:
            break;
    }
}

void gatt_svr_subscribe_cb(struct ble_gap_event *event) {
    if (event->subscribe.attr_handle == chr_val_handle_tx) {
        chr_conn_handle_tx = event->subscribe.conn_handle;
        indicate_status_tx.chr_conn_handle_status = true;
        indicate_status_tx.ind_status = event->subscribe.cur_indicate || event->subscribe.cur_notify; // true if indication enabled
    }

    if (event->subscribe.attr_handle == chr_val_handle_rx) {
        chr_conn_handle_rx = event->subscribe.conn_handle;
        notify_status_rx.chr_conn_handle_status = true;
        notify_status_rx.notify_status = event->subscribe.cur_notify;
    }
}

int gatt_svc_init() {
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) return rc;
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) return rc;
    return 0;
}

void send_indication(void){
    chr_send_indication(&indicate_status_tx, chr_conn_handle_tx, chr_val_handle_tx);
}

uint16_t a_ble_get_tx_conn_handle(void) {
    return chr_conn_handle_tx;
}

void gatt_reset_conn_handle(void) {
    chr_conn_handle_tx = 0;
    chr_conn_handle_rx = 0;
}

uint16_t a_ble_get_tx_val_handle(void) {
    return chr_val_handle_tx;
}

bool gatt_can_send_tx_indication(void) {
    return indicate_status_tx.chr_conn_handle_status && indicate_status_tx.ind_status;
}


