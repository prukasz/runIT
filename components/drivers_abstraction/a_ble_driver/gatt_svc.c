#include "common.h"
#include "gatt_svc.h"
#include "gatt_uuids.h"

// Static pointer to the user-provided callback
static esp_err_t (*_ble_rx_callback)(const uint8_t* data, size_t len) = NULL;

/**
 * @brief Registers a callback to be executed whenever the phone writes to the VM_IN characteristic.
 */
void gatt_svc_add_callback_on_write(esp_err_t (*callback)(const uint8_t* data, size_t len)) {
    _ble_rx_callback = callback;
}

/*-----callback when characteristics is accesed----*/
static int chr_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);

/*-----callback when characteristics descriptor is accesed----*/
static int chr_desc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                           struct ble_gatt_access_ctxt *ctxt, void *arg);

/*----Characteristic value handle----*/
static uint16_t chr_val_handle_vm_out;
static uint16_t chr_val_handle_vm_in;
  
/*----Characteristic descriptor value handle----*/
static uint16_t chr_desc_val_handle_vm_out;
static uint16_t chr_desc_val_handle_vm_in;

/*----Characteristics connection handle----
Represents what device is accesing characteristics*/
static uint16_t chr_conn_handle_vm_out = 0;
static uint16_t chr_conn_handle_vm_in = 0;

/*User (readable) descriptor value as text do be displayed*/
static const char chr_desc_vm_out[] = "VM Output";
static const char chr_desc_vm_in[]  = "VM Input";


/*Indication status struct
Indication is like notification that requires confirmation of being receivied by peer*/
static indicate_status_t indicate_status_vm_out = {.ind_status = 0, .chr_conn_handle_status = 0};
static notify_status_t notify_status_vm_in  = {.notify_status = 0, .chr_conn_handle_status = 0};

/* Mutex protecting mbuf alloc + notify + free-on-error sequences from concurrent tasks */
static SemaphoreHandle_t notify_mutex = NULL;

/*This is main struct for definition of all services and characteristics used by NIMBLE*/
static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &uuid_svc_vm.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = &uuid_chr_vm_out.u,
                .access_cb = chr_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY | BLE_GATT_CHR_F_INDICATE,
                .val_handle = &chr_val_handle_vm_out,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .access_cb = chr_desc_access_cb,
                        .att_flags = BLE_ATT_F_READ,
                        .arg = (void*)chr_desc_vm_out,
                    },
                    {0}
                }
            },
            {
                .uuid = &uuid_chr_vm_in.u,
                .access_cb = chr_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &chr_val_handle_vm_in,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2901),
                        .access_cb = chr_desc_access_cb,
                        .att_flags = BLE_ATT_F_READ,
                        .arg = (void*)chr_desc_vm_in
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
    /*check what characteristics was accesed via comparing handle*/
    if (attr_handle == chr_val_handle_vm_out) {
        //read only option
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            uint8_t *data_out = NULL;
            size_t len = 0;
            int res = os_mbuf_append(ctxt->om, data_out, len);
            if (res != 0) {return BLE_ATT_ERR_INSUFFICIENT_RES;}
            ESP_LOGI(TAG, "Sent %d bytes to device %d", len, conn_handle);
            return 0;
        }else{
            return BLE_ATT_ERR_READ_NOT_PERMITTED;}
    }else if (attr_handle == chr_val_handle_vm_in){
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            size_t len = OS_MBUF_PKTLEN(ctxt->om);
            
            if (len > 0) {
                // Use a temporary buffer on the stack for speed (up to 512 bytes)
                uint8_t data_buffer[512]; 
                if (len > 512) len = 512; // Safety truncation

                // Copy from the mbuf into our flat array
                os_mbuf_copydata(ctxt->om, 0, len, data_buffer);
                _ble_rx_callback(data_buffer, len);
            }
            return 0;
        }
        return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
    } // end if attr_handle emu_in
    return BLE_ATT_ERR_UNLIKELY;
}

/*-----callback when characteristics descriptor is accesed----*/
static int chr_desc_access_cb(uint16_t conn_handle, uint16_t attr_handle,
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
                if (ctxt->dsc.chr_def->val_handle == &chr_val_handle_vm_out) {
                    chr_desc_val_handle_vm_out = ctxt->dsc.handle;
                } else if (ctxt->dsc.chr_def->val_handle == &chr_val_handle_vm_in) {
                    chr_desc_val_handle_vm_in = ctxt->dsc.handle;
                }   
            }
            break;
        default:
            break;
    }
}

void gatt_svr_subscribe_cb(struct ble_gap_event *event) {
    if (event->subscribe.attr_handle == chr_val_handle_vm_out) {
        chr_conn_handle_vm_out = event->subscribe.conn_handle;
        indicate_status_vm_out.chr_conn_handle_status = true;
        indicate_status_vm_out.ind_status = event->subscribe.cur_indicate || event->subscribe.cur_notify; // true if indication enabled
    }

    if (event->subscribe.attr_handle == chr_val_handle_vm_in) {
        chr_conn_handle_vm_in = event->subscribe.conn_handle;
        notify_status_vm_in.chr_conn_handle_status = true;
        notify_status_vm_in.notify_status = event->subscribe.cur_notify;
    }
}

int gatt_svc_init() {
    if (!notify_mutex) notify_mutex = xSemaphoreCreateMutex();
    ble_svc_gatt_init();
    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    if (rc != 0) return rc;
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    if (rc != 0) return rc;
    return 0;
}

void send_indication(void){
    chr_send_indication(&indicate_status_vm_out, chr_conn_handle_vm_out, chr_val_handle_vm_out);
}

uint16_t gatt_get_vm_out_conn_handle(void) {
    return chr_conn_handle_vm_out;
}

uint16_t gatt_get_vm_out_val_handle(void) {
    return chr_val_handle_vm_out;
}

bool gatt_can_send_vm_out_indication(void) {
    return indicate_status_vm_out.chr_conn_handle_status && indicate_status_vm_out.ind_status;
}


void gatt_notify_ready(void) {
    if (!notify_status_vm_in.notify_status || !notify_status_vm_in.chr_conn_handle_status) return;
    static const uint8_t ready_byte = 0x00;

    xSemaphoreTake(notify_mutex, portMAX_DELAY);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(&ready_byte, sizeof(ready_byte));
    if (!om) { xSemaphoreGive(notify_mutex); return; }
    (void)ble_gatts_notify_custom(chr_conn_handle_vm_in, chr_val_handle_vm_in, om);
    xSemaphoreGive(notify_mutex);
}

int gatt_send_notify(const uint8_t *data, size_t len) {
    xSemaphoreTake(notify_mutex, portMAX_DELAY);
    struct os_mbuf *om = ble_hs_mbuf_from_flat(data, len);
    if (!om) { xSemaphoreGive(notify_mutex); return -1; }
    int res = ble_gatts_notify_custom(chr_conn_handle_vm_out, chr_val_handle_vm_out, om);
    xSemaphoreGive(notify_mutex);
    return res;
}



