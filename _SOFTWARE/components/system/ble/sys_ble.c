#include "sys_ble.h"
#include <freertos/task.h>
#include <esp_log.h>
#include <string.h>
#include <stdlib.h>
#include "utils.h"
#include "a_ble.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = __FILE_NAME__;

#undef R_MUTEX_LOCK
#undef R_MUTEX_UNLOCK
#define R_MUTEX_LOCK(mutex_handle, timeout_ticks) R_RECURSIVE_MUTEX_LOCK(mutex_handle, timeout_ticks)
#define R_MUTEX_UNLOCK(mutex_handle) R_RECURSIVE_MUTEX_UNLOCK(mutex_handle)

#define CONFIG_PRIORITY_BLE_MANAGER_TASK 5
#define BLE_TASK_STACK_SIZE 4096
#define MAX_TOTAL_TX_SLOTS 24

#define BIT_ON_INDICATION_COMPLETE    (1 << 0)
#define BIT_ON_INDICATION_TIMEOUT     (1 << 1)
#define BIT_ON_NOTIFICATION_COMPLETE  (1 << 2)
#define BIT_START_TX                  (1 << 3)

R_TASK_DEFINE(m_ble_task, BLE_TASK_STACK_SIZE);
R_RECURSIVE_MUTEX_DEFINE(sys_ble_mutex);

typedef struct sys_ble_char_node {
    sys_ble_char_cfg_t cfg;
    uint16_t val_handle;
    
    // RX buffer
    RingbufHandle_t rx_buff;
    SemaphoreHandle_t rx_sem;
    
    // TX buffers
    sys_ble_tx_slot_t tx_slots[MAX_TX_BUFFERS];
    uint8_t tx_slot_count;

    struct sys_ble_char_node *next;
} sys_ble_char_node_t;

typedef struct sys_ble_svc_node {
    sys_ble_svc_cfg_t cfg;
    sys_ble_char_node_t *chars;
    bool registered;
    struct ble_gatt_svc_def *compiled_def; // Heap allocated for this specific service
    struct sys_ble_svc_node *next;
} sys_ble_svc_node_t;

typedef struct {
    sys_ble_tx_slot_t *slot;
    sys_ble_char_node_t *chr;
} sys_ble_active_slot_t;

typedef struct {
    sys_ble_svc_node_t *services;
    uint16_t route_masks[SYS_BLE_EVENT_MAX];
    bool initialized;
    bool driver_started;
    uint16_t conn_handle;
    bool is_connected;
    esp_err_t last_error;
    uint32_t rx_overflow_count;
    
    // Flat compiled active TX slots
    sys_ble_active_slot_t tx_slots[MAX_TOTAL_TX_SLOTS];
    uint8_t tx_slot_count;
    
    // Internal event group for sending task
    EventGroupHandle_t tx_event_group;
    
    // Pointer to compiled GATT database definitions for initial cleanup
    struct ble_gatt_svc_def *compiled_db;
} sys_ble_ctx_t;

static sys_ble_ctx_t g_ble_ctx = {0};

static esp_err_t sys_ble_tx_dequeue_slot(sys_ble_tx_slot_t *slot, uint8_t* data, size_t* len, size_t max_payload);
static void sort_tx_slots(void);
static void rebuild_active_tx_slots(void);
static void free_compiled_gatt_db(struct ble_gatt_svc_def *svcs);
static ble_uuid_t *malloc_uuid(uint16_t uuid16);
static void sys_ble_task_func(void *pvParameters);

// Callback functions for BLE driver events
static void sys_ble_on_driver_connect(uint16_t conn_handle);
static void sys_ble_on_driver_disconnect(uint16_t conn_handle, int reason);
static void sys_ble_on_driver_failure(esp_err_t error);

// access callbacks
static int sys_ble_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg);
static int sys_ble_gatt_dsc_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg);

// Entity search helpers
static sys_ble_char_node_t *find_char_by_uuid(uint16_t char_uuid);
static sys_ble_svc_node_t *find_svc_by_uuid(uint16_t svc_uuid);
static struct ble_gatt_chr_def *compile_chars(const sys_ble_char_node_t *chars_head, bool *out_success);
static esp_err_t populate_svc_def(struct ble_gatt_svc_def *svc_def, const sys_ble_svc_node_t *s);

/*****************************************************************************************/
/* Public API                                                                            */
/*****************************************************************************************/

#define OWNER OWNER_SYS_BLE_CREATE
status_rep_t sys_ble_init(void) {
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    
    if (g_ble_ctx.initialized) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_OK;
    }
    
    g_ble_ctx.tx_event_group = xEventGroupCreate();
    if (!g_ble_ctx.tx_event_group) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NO_MEM, OWNER, 0, STATUS_PAYLOAD_UNKNOWN);
    }
    
    g_ble_ctx.initialized = true;
    R_MUTEX_UNLOCK(sys_ble_mutex);
    
    ESP_LOGI(TAG, "BLE Manager initialized successfully");
    return STA_OK;
}

status_rep_t sys_ble_add_callback(sys_ble_events_e on_event, uint16_t route_mask) {
    if (on_event >= SYS_BLE_EVENT_MAX) {
        return STA_C(ERR_INVALID_ARG, OWNER, 0, STATUS_PAYLOAD_UNKNOWN);
    }
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    g_ble_ctx.route_masks[on_event] = route_mask;
    R_MUTEX_UNLOCK(sys_ble_mutex);
    return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_SERVICE_CREATE
status_rep_t sys_ble_service_create(const sys_ble_svc_cfg_t *cfg) {
    CHECK_NOT_NULL_R(cfg);
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    
    if (find_svc_by_uuid(cfg->uuid)) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_DEV_ALREADY_EXISTS, OWNER, cfg->uuid, STATUS_PAYLOAD_UNKNOWN);
    }
    
    sys_ble_svc_node_t *new_svc = calloc(1, sizeof(sys_ble_svc_node_t));
    if (!new_svc) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NO_MEM, OWNER, cfg->uuid, STATUS_PAYLOAD_UNKNOWN);
    }
    
    new_svc->cfg = *cfg;
    new_svc->registered = false;
    new_svc->compiled_def = NULL;
    
    LL_APPEND(g_ble_ctx.services, new_svc);
    R_MUTEX_UNLOCK(sys_ble_mutex);
    
    ESP_LOGI(TAG, "Created BLE service UUID 0x%04X", cfg->uuid);
    return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_SERVICE_REMOVE
status_rep_t sys_ble_service_remove(uint16_t svc_uuid) {
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    
    sys_ble_svc_node_t *target = find_svc_by_uuid(svc_uuid);
    if (!target) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NOT_FOUND, OWNER, svc_uuid, STATUS_PAYLOAD_UNKNOWN);
    }
    
    // Delete from stack dynamically if driver started and it is registered
    if (g_ble_ctx.driver_started && target->registered) {
        ble_uuid16_t temp_uuid;
        temp_uuid.u.type = BLE_UUID_TYPE_16;
        temp_uuid.value = target->cfg.uuid;
        
        int rc = ble_gatts_delete_svc((const ble_uuid_t *)&temp_uuid);
        if (rc != 0) {
            ESP_LOGE(TAG, "Failed to delete service 0x%04X from NimBLE: %d", svc_uuid, rc);
            R_MUTEX_UNLOCK(sys_ble_mutex);
            return STA_C(ERR_HARDWARE_FAULT, OWNER, rc, STATUS_PAYLOAD_UNKNOWN);
        }
    }
    
    // Free compiled def if runtime added
    if (target->compiled_def) {
        free_compiled_gatt_db(target->compiled_def);
    }
    
    // Free all characteristics of the service
    sys_ble_char_node_t *c, *tmp;
    LL_FOREACH_SAFE(target->chars, c, tmp) {
        if (c->rx_buff) vRingbufferDelete(c->rx_buff);
        if (c->rx_sem) vSemaphoreDelete(c->rx_sem);
        for (int i = 0; i < c->tx_slot_count; i++) {
            if (c->tx_slots[i].tx_buff) vRingbufferDelete(c->tx_slots[i].tx_buff);
        }
        free(c);
    }
    
    LL_DELETE(g_ble_ctx.services, target);
    free(target);
    
    rebuild_active_tx_slots(); // Prevent use-after-free of freed TX slots
    
    R_MUTEX_UNLOCK(sys_ble_mutex);
    ESP_LOGI(TAG, "Removed BLE service UUID 0x%04X", svc_uuid);
    return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_CHAR_CREATE
status_rep_t sys_ble_char_create(uint16_t svc_uuid, const sys_ble_char_create_t *cfg) {
    CHECK_NOT_NULL_R(cfg);
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    
    sys_ble_svc_node_t *svc = find_svc_by_uuid(svc_uuid);
    if (!svc) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NOT_FOUND, OWNER, svc_uuid, STATUS_PAYLOAD_UNKNOWN);
    }
    
    if (find_char_by_uuid(cfg->info.uuid)) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_DEV_ALREADY_EXISTS, OWNER, cfg->info.uuid, STATUS_PAYLOAD_UNKNOWN);
    }
    
    sys_ble_char_node_t *new_char = calloc(1, sizeof(sys_ble_char_node_t));
    if (!new_char) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NO_MEM, OWNER, cfg->info.uuid, STATUS_PAYLOAD_UNKNOWN);
    }
    
    new_char->cfg = cfg->info;
    
    // Allocate RX Ring Buffer and Semaphore if size > 0
    if (cfg->rx_buffer_size > 0) {
        new_char->rx_buff = xRingbufferCreate(cfg->rx_buffer_size, RINGBUF_TYPE_BYTEBUF);
        new_char->rx_sem = xSemaphoreCreateBinary();
        if (!new_char->rx_buff || !new_char->rx_sem) {
            if (new_char->rx_buff) vRingbufferDelete(new_char->rx_buff);
            if (new_char->rx_sem) vSemaphoreDelete(new_char->rx_sem);
            free(new_char);
            R_MUTEX_UNLOCK(sys_ble_mutex);
            return STA_C(ERR_NO_MEM, OWNER, cfg->info.uuid, STATUS_PAYLOAD_UNKNOWN);
        }
    }
    
    LL_APPEND(svc->chars, new_char);
    R_MUTEX_UNLOCK(sys_ble_mutex);
    
    ESP_LOGI(TAG, "Created characteristic UUID 0x%04X under service UUID 0x%04X", cfg->info.uuid, svc_uuid);
    return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_CHAR_REMOVE
status_rep_t sys_ble_char_remove(uint16_t svc_uuid, uint16_t char_uuid) {
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    
    sys_ble_svc_node_t *svc = find_svc_by_uuid(svc_uuid);
    if (!svc) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NOT_FOUND, OWNER, svc_uuid, STATUS_PAYLOAD_UNKNOWN);
    }
    
    sys_ble_char_node_t *target = NULL;
    sys_ble_char_node_t *c;
    LL_FOREACH(svc->chars, c) {
        if (c->cfg.uuid == char_uuid) {
            target = c;
            break;
        }
    }
    
    if (!target) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NOT_FOUND, OWNER, char_uuid, STATUS_PAYLOAD_UNKNOWN);
    }
    
    // Free buffers and semaphores
    if (target->rx_buff) vRingbufferDelete(target->rx_buff);
    if (target->rx_sem) vSemaphoreDelete(target->rx_sem);
    for (int i = 0; i < target->tx_slot_count; i++) {
        if (target->tx_slots[i].tx_buff) vRingbufferDelete(target->tx_slots[i].tx_buff);
    }
    
    LL_DELETE(svc->chars, target);
    free(target);
    
    rebuild_active_tx_slots(); // Prevent use-after-free of freed TX slots
    
    R_MUTEX_UNLOCK(sys_ble_mutex);
    ESP_LOGI(TAG, "Removed BLE characteristic UUID 0x%04X", char_uuid);
    return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_CHAR_ASSIGN_TX
status_rep_t sys_ble_char_assign_tx_buffer(uint16_t char_uuid, const sys_ble_tx_buf_cfg_t *buf_cfg) {
    CHECK_NOT_NULL_R(buf_cfg);
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    
    sys_ble_char_node_t *c = find_char_by_uuid(char_uuid);
    if (!c) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NOT_FOUND, OWNER, char_uuid, STATUS_PAYLOAD_UNKNOWN);
    }
    
    if (c->tx_slot_count >= MAX_TX_BUFFERS) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NO_MEM, OWNER, char_uuid, STATUS_PAYLOAD_UNKNOWN);
    }
    
    // Check if buffer_id already exists in this characteristic
    for (int i = 0; i < c->tx_slot_count; i++) {
        if (c->tx_slots[i].buffer_id == buf_cfg->buffer_id) {
            R_MUTEX_UNLOCK(sys_ble_mutex);
            return STA_C(ERR_DEV_ALREADY_EXISTS, OWNER, buf_cfg->buffer_id, STATUS_PAYLOAD_UNKNOWN);
        }
    }
    
    // Create Ring Buffer
    RingbufHandle_t tx_buffer = xRingbufferCreate(buf_cfg->size, buf_cfg->buff_type);
    if (!tx_buffer) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NO_MEM, OWNER, buf_cfg->buffer_id, STATUS_PAYLOAD_UNKNOWN);
    }
    
    sys_ble_tx_slot_t *slot = &c->tx_slots[c->tx_slot_count];
    slot->buffer_id = buf_cfg->buffer_id;
    slot->tx_buff = tx_buffer;
    slot->buff_type = buf_cfg->buff_type;
    slot->item_size = buf_cfg->item_size;
    slot->auto_pack = buf_cfg->auto_pack;
    slot->header = buf_cfg->header;
    slot->priority = buf_cfg->priority;
    slot->is_indication = buf_cfg->is_indication;
    
    c->tx_slot_count++;
    R_MUTEX_UNLOCK(sys_ble_mutex);
    
    ESP_LOGI(TAG, "Assigned TX buffer ID %d to characteristic UUID 0x%04X (priority=%d)",
             buf_cfg->buffer_id, char_uuid, buf_cfg->priority);
    return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_GET_STATUS
status_rep_t sys_ble_char_get_rx_semaphore(uint16_t char_uuid, SemaphoreHandle_t *out_sem) {
    CHECK_NOT_NULL_R(out_sem);
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    
    sys_ble_char_node_t *c = find_char_by_uuid(char_uuid);
    if (!c) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NOT_FOUND, OWNER, char_uuid, STATUS_PAYLOAD_UNKNOWN);
    }
    
    *out_sem = c->rx_sem;
    R_MUTEX_UNLOCK(sys_ble_mutex);
    return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_SEND
status_rep_t sys_ble_char_rx_dequeue(uint16_t char_uuid, uint8_t *buffer, size_t max_len, size_t *out_len) {
    CHECK_NOT_NULL_R(buffer);
    CHECK_NOT_NULL_R(out_len);
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    
    sys_ble_char_node_t *c = find_char_by_uuid(char_uuid);
    if (!c) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NOT_FOUND, OWNER, char_uuid, STATUS_PAYLOAD_UNKNOWN);
    }
    
    if (!c->rx_buff) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_INVALID_STATE, OWNER, char_uuid, STATUS_PAYLOAD_UNKNOWN);
    }
    
    size_t item_size = 0;
    void *item = xRingbufferReceiveUpTo(c->rx_buff, &item_size, 0, max_len);
    if (!item) {
        *out_len = 0;
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_OK;
    }
    
    memcpy(buffer, item, item_size);
    *out_len = item_size;
    vRingbufferReturnItem(c->rx_buff, item);
    
    R_MUTEX_UNLOCK(sys_ble_mutex);
    return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_SEND
status_rep_t sys_ble_char_send(uint16_t char_uuid, uint8_t buffer_id, const uint8_t *data, size_t len, bool return_when_full) {
    CHECK_NOT_NULL_R(data);
    if (len == 0) return STA_OK;
    
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    
    sys_ble_char_node_t *c = find_char_by_uuid(char_uuid);
    if (!c) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NOT_FOUND, OWNER, char_uuid, STATUS_PAYLOAD_UNKNOWN);
    }
    
    // Find TX slot
    sys_ble_tx_slot_t *slot = NULL;
    for (int i = 0; i < c->tx_slot_count; i++) {
        if (c->tx_slots[i].buffer_id == buffer_id) {
            slot = &c->tx_slots[i];
            break;
        }
    }
    
    if (!slot) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NOT_FOUND, OWNER, buffer_id, STATUS_PAYLOAD_UNKNOWN);
    }
    
    uint32_t wait_time_ms = return_when_full ? 0 : 100;
    if (xRingbufferSend(slot->tx_buff, data, len, pdMS_TO_TICKS(wait_time_ms)) != pdTRUE) {
        R_MUTEX_UNLOCK(sys_ble_mutex);
        return STA_C(ERR_NO_MEM, OWNER, buffer_id, STATUS_PAYLOAD_UNKNOWN);
    }
    
    // Unblock BLE sending task
    xEventGroupSetBits(g_ble_ctx.tx_event_group, BIT_START_TX);
    
    R_MUTEX_UNLOCK(sys_ble_mutex);
    return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_DATABASE_SYNC
status_rep_t sys_ble_database_sync(void) {
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    
    if (!g_ble_ctx.driver_started) {
        // Compile initial database (all services currently registered)
        uint16_t num_svcs = 0;
        sys_ble_svc_node_t *s;
        LL_FOREACH(g_ble_ctx.services, s) {
            num_svcs++;
        }
        
        struct ble_gatt_svc_def *svcs = calloc(num_svcs + 1, sizeof(struct ble_gatt_svc_def));
        if (!svcs) {
            R_MUTEX_UNLOCK(sys_ble_mutex);
            return STA_C(ERR_NO_MEM, OWNER, 0, STATUS_PAYLOAD_UNKNOWN);
        }
        
        int s_idx = 0;
        LL_FOREACH(g_ble_ctx.services, s) {
            if (populate_svc_def(&svcs[s_idx], s) != ESP_OK) {
                free_compiled_gatt_db(svcs);
                R_MUTEX_UNLOCK(sys_ble_mutex);
                return STA_C(ERR_NO_MEM, OWNER, s->cfg.uuid, STATUS_PAYLOAD_UNKNOWN);
            }
            s->registered = true;
            s->compiled_def = NULL; // initial svcs are freed together
            s_idx++;
        }
        
        g_ble_ctx.compiled_db = svcs;
        
        static a_ble_host_cfg_t host_cfg = {0};
        host_cfg.on_connect = sys_ble_on_driver_connect;
        host_cfg.on_disconnect = sys_ble_on_driver_disconnect;
        host_cfg.on_failure = sys_ble_on_driver_failure;
        
        host_cfg.tx_event_group = g_ble_ctx.tx_event_group;
        host_cfg.bit_tx_indication_complete = BIT_ON_INDICATION_COMPLETE;
        host_cfg.bit_tx_indication_timeout = BIT_ON_INDICATION_TIMEOUT;
        host_cfg.bit_tx_notification_complete = BIT_ON_NOTIFICATION_COMPLETE;
        
        R_TASK_START_ON_CORE(m_ble_task, &sys_ble_task_func, &g_ble_ctx, CONFIG_PRIORITY_BLE_MANAGER_TASK, 0);
        
        R_MUTEX_UNLOCK(sys_ble_mutex);
        esp_err_t err = a_ble_init(&host_cfg, svcs);
        R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start BLE Driver: %d", err);
            free_compiled_gatt_db(svcs);
            g_ble_ctx.compiled_db = NULL;
            R_MUTEX_UNLOCK(sys_ble_mutex);
            return STA_C(ERR_ESP, OWNER, err, STATUS_PAYLOAD_UNKNOWN);
        }
        g_ble_ctx.driver_started = true;
        
    } else {
        // Compile and add dynamic services for newly added services (registered == false)
        sys_ble_svc_node_t *s;
        LL_FOREACH(g_ble_ctx.services, s) {
            if (s->registered) continue;
            
            struct ble_gatt_svc_def *svcs = calloc(2, sizeof(struct ble_gatt_svc_def));
            if (!svcs) {
                R_MUTEX_UNLOCK(sys_ble_mutex);
                return STA_C(ERR_NO_MEM, OWNER, s->cfg.uuid, STATUS_PAYLOAD_UNKNOWN);
            }
            
            if (populate_svc_def(&svcs[0], s) != ESP_OK) {
                free(svcs);
                R_MUTEX_UNLOCK(sys_ble_mutex);
                return STA_C(ERR_NO_MEM, OWNER, s->cfg.uuid, STATUS_PAYLOAD_UNKNOWN);
            }
            
            // Register dynamically using NimBLE runtime dynamic service API
            R_MUTEX_UNLOCK(sys_ble_mutex);
            int rc = ble_gatts_add_dynamic_svcs(svcs);
            R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);

            if (rc != 0) {
                ESP_LOGE(TAG, "Failed to dynamically add service UUID 0x%04X: %d", s->cfg.uuid, rc);
                free_compiled_gatt_db(svcs);
                R_MUTEX_UNLOCK(sys_ble_mutex);
                return STA_C(ERR_HARDWARE_FAULT, OWNER, rc, STATUS_PAYLOAD_UNKNOWN);
            }
            
            s->registered = true;
            s->compiled_def = svcs;
            ESP_LOGI(TAG, "Dynamically registered service UUID 0x%04X", s->cfg.uuid);
        }
    }
    
    // Rebuild active TX slot mapping with new handles and sort
    rebuild_active_tx_slots();
    
    R_MUTEX_UNLOCK(sys_ble_mutex);
    ESP_LOGI(TAG, "BLE database compilation and stack sync complete");
    return STA_OK;
}
#undef OWNER

#define OWNER OWNER_SYS_BLE_GET_STATUS
status_rep_t sys_ble_get_status(sys_ble_status_t *out_status) {
    CHECK_NOT_NULL_R(out_status);
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    
    out_status->is_connected = g_ble_ctx.is_connected;
    out_status->mtu_size = a_ble_get_mtu();
    out_status->rx_overflow_count = g_ble_ctx.rx_overflow_count;
    out_status->last_error = g_ble_ctx.last_error;
    
    R_MUTEX_UNLOCK(sys_ble_mutex);
    return STA_OK;
}
#undef OWNER

void sys_ble_on_subscribe(uint16_t conn_handle, uint16_t attr_handle, bool indicate, bool notify) {
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    
    sys_ble_svc_node_t *s;
    LL_FOREACH(g_ble_ctx.services, s) {
        sys_ble_char_node_t *c;
        LL_FOREACH(s->chars, c) {
            if (c->val_handle == attr_handle) {
                ESP_LOGI(TAG, "Client subscription updated: uuid=0x%04X conn_handle=%d indicate=%d notify=%d",
                         c->cfg.uuid, conn_handle, indicate, notify);
                
                R_MUTEX_UNLOCK(sys_ble_mutex);
                return;
            }
        }
    }
    
    R_MUTEX_UNLOCK(sys_ble_mutex);
}

/*****************************************************************************************/
/* Private Helper Functions                                                              */
/*****************************************************************************************/

static void rebuild_active_tx_slots(void) {
    g_ble_ctx.tx_slot_count = 0;
    
    sys_ble_svc_node_t *s;
    LL_FOREACH(g_ble_ctx.services, s) {
        sys_ble_char_node_t *c;
        LL_FOREACH(s->chars, c) {
            for (int i = 0; i < c->tx_slot_count; i++) {
                if (g_ble_ctx.tx_slot_count >= MAX_TOTAL_TX_SLOTS) {
                    ESP_LOGE(TAG, "Exceeded maximum total TX slots (%d)", MAX_TOTAL_TX_SLOTS);
                    break;
                }
                g_ble_ctx.tx_slots[g_ble_ctx.tx_slot_count].slot = &c->tx_slots[i];
                g_ble_ctx.tx_slots[g_ble_ctx.tx_slot_count].chr = c;
                g_ble_ctx.tx_slot_count++;
            }
        }
    }
    
    sort_tx_slots();
}

static void sort_tx_slots(void) {
    if (g_ble_ctx.tx_slot_count < 2) return;
    for (int i = 0; i < g_ble_ctx.tx_slot_count - 1; i++) {
        for (int j = i + 1; j < g_ble_ctx.tx_slot_count; j++) {
            if (g_ble_ctx.tx_slots[i].slot->priority > g_ble_ctx.tx_slots[j].slot->priority) {
                sys_ble_active_slot_t temp = g_ble_ctx.tx_slots[i];
                g_ble_ctx.tx_slots[i] = g_ble_ctx.tx_slots[j];
                g_ble_ctx.tx_slots[j] = temp;
            }
        }
    }
}

static void free_compiled_gatt_db(struct ble_gatt_svc_def *svcs) {
    if (!svcs) return;
    for (int i = 0; svcs[i].type != 0; i++) {
        if (svcs[i].uuid) {
            free((void *)svcs[i].uuid);
        }
        
        struct ble_gatt_chr_def *chrs = (struct ble_gatt_chr_def *)svcs[i].characteristics;
        if (chrs) {
            for (int j = 0; chrs[j].uuid != NULL; j++) {
                free((void *)chrs[j].uuid);
                
                struct ble_gatt_dsc_def *dscs = (struct ble_gatt_dsc_def *)chrs[j].descriptors;
                if (dscs) {
                    free(dscs);
                }
            }
            free(chrs);
        }
    }
    free(svcs);
}

static ble_uuid_t *malloc_uuid(uint16_t uuid16) {
    ble_uuid16_t *u16 = malloc(sizeof(ble_uuid16_t));
    if (u16) {
        u16->u.type = BLE_UUID_TYPE_16;
        u16->value = uuid16;
    }
    return (ble_uuid_t *)u16;
}

static void sys_ble_on_driver_connect(uint16_t conn_handle) {
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    g_ble_ctx.conn_handle = conn_handle;
    g_ble_ctx.is_connected = true;
    uint16_t mask = g_ble_ctx.route_masks[SYS_BLE_EVENT_CONNECT];
    R_MUTEX_UNLOCK(sys_ble_mutex);
    
    xEventGroupSetBits(g_ble_ctx.tx_event_group, BIT_START_TX);
    
    SYS_BLE_CB(SYS_BLE_EVENT_CONNECT, conn_handle, mask);
}

static void sys_ble_on_driver_disconnect(uint16_t conn_handle, int reason) {
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    g_ble_ctx.conn_handle = 0;
    g_ble_ctx.is_connected = false;
    uint16_t mask = g_ble_ctx.route_masks[SYS_BLE_EVENT_DISCONNECT];
    R_MUTEX_UNLOCK(sys_ble_mutex);
    
    SYS_BLE_CB(SYS_BLE_EVENT_DISCONNECT, reason, mask);
}

static void sys_ble_on_driver_failure(esp_err_t error) {
    R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
    g_ble_ctx.last_error = error;
    uint16_t mask = g_ble_ctx.route_masks[SYS_BLE_EVENT_FAILURE];
    R_MUTEX_UNLOCK(sys_ble_mutex);
    
    SYS_BLE_CB(SYS_BLE_EVENT_FAILURE, error, mask);
}

static int sys_ble_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg) {
    sys_ble_char_node_t *char_node = (sys_ble_char_node_t *)arg;
    if (!char_node) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        size_t len = OS_MBUF_PKTLEN(ctxt->om);
        if (len > 0) {
            if (!char_node->rx_buff) {
                return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
            }
            uint8_t data_buffer[512];
            size_t copy_len = len > sizeof(data_buffer) ? sizeof(data_buffer) : len;
            os_mbuf_copydata(ctxt->om, 0, copy_len, data_buffer);

            if (xRingbufferSend(char_node->rx_buff, data_buffer, copy_len, 0) != pdTRUE) {
                ESP_LOGW(TAG, "RX buffer overflow on char uuid 0x%04X", char_node->cfg.uuid);
                R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
                g_ble_ctx.rx_overflow_count++;
                uint16_t mask = g_ble_ctx.route_masks[SYS_BLE_EVENT_FAILURE];
                R_MUTEX_UNLOCK(sys_ble_mutex);
                SYS_BLE_CB(SYS_BLE_EVENT_FAILURE, ESP_FAIL, mask);
            } else {
                xSemaphoreGive(char_node->rx_sem);
            }
        }
        return 0;
    } else if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return 0;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static int sys_ble_gatt_dsc_cb(uint16_t conn_handle, uint16_t attr_handle,
                                struct ble_gatt_access_ctxt *ctxt, void *arg) {
    const char *desc = (const char *)arg;
    if (!desc) return BLE_ATT_ERR_UNLIKELY;
    return os_mbuf_append(ctxt->om, desc, strlen(desc)) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

/*****************************************************************************************/
/* TX Task & Dequeue Logic                                                                */
/*****************************************************************************************/

static void sys_ble_task_func(void *pvParameters) {
    (void)pvParameters;
    while (1) {
        xEventGroupWaitBits(g_ble_ctx.tx_event_group, BIT_START_TX, pdTRUE, pdFALSE, portMAX_DELAY);
        bool data_sent;

        do {
            uint8_t tx_data[527] = {0};
            data_sent = false;

            uint16_t current_mtu = a_ble_get_mtu();
            size_t max_payload = (current_mtu > 3) ? (current_mtu - 3) : 20;

            if (max_payload > sizeof(tx_data)) {
                max_payload = sizeof(tx_data);
            }

            R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
            if (!g_ble_ctx.is_connected) {
                R_MUTEX_UNLOCK(sys_ble_mutex);
                break;
            }

            for (int i = 0; i < g_ble_ctx.tx_slot_count; i++) {
                sys_ble_tx_slot_t *slot = g_ble_ctx.tx_slots[i].slot;
                sys_ble_char_node_t *c = g_ble_ctx.tx_slots[i].chr;
                if (slot == NULL || slot->tx_buff == NULL) continue;
                if (c->val_handle == 0) continue; // Not registered by stack yet

                size_t tx_len = 0;
                esp_err_t deq_res = sys_ble_tx_dequeue_slot(slot, tx_data, &tx_len, max_payload);

                if (deq_res == ESP_OK && tx_len > 0) {
                    uint16_t conn_handle = g_ble_ctx.conn_handle;
                    uint16_t val_handle = c->val_handle; // Use live handle reference
                    R_MUTEX_UNLOCK(sys_ble_mutex);

                    REPEAT:;
                    esp_err_t send_res = a_ble_send(conn_handle, val_handle, tx_data, tx_len, slot->is_indication);

                    if (send_res == ESP_OK) {
                        data_sent = true;
                        R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
                        break;
                    } else if (send_res == ESP_ERR_NO_MEM) {
                        xEventGroupWaitBits(g_ble_ctx.tx_event_group,
                                             BIT_ON_INDICATION_COMPLETE | BIT_ON_NOTIFICATION_COMPLETE | BIT_ON_INDICATION_TIMEOUT | BIT_START_TX,
                                             pdTRUE, pdFALSE, pdMS_TO_TICKS(100));
                        goto REPEAT;
                    } else {
                        R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
                        g_ble_ctx.last_error = send_res;
                        uint16_t mask = g_ble_ctx.route_masks[SYS_BLE_EVENT_FAILURE];
                        R_MUTEX_UNLOCK(sys_ble_mutex);
                        SYS_BLE_CB(SYS_BLE_EVENT_FAILURE, send_res, mask);
                        R_MUTEX_LOCK(sys_ble_mutex, WAIT_FOREVER);
                        break;
                    }
                }
            }
            R_MUTEX_UNLOCK(sys_ble_mutex);
        } while (data_sent);
    }
}

static esp_err_t sys_ble_tx_dequeue_slot(sys_ble_tx_slot_t *slot, uint8_t* data, size_t* len, size_t max_payload) {
    if (max_payload < 2) return ESP_ERR_INVALID_SIZE;

    if (slot->buff_type == RINGBUF_TYPE_NOSPLIT) {
        size_t item_size = 0;
        void *item = xRingbufferReceive(slot->tx_buff, &item_size, 0);
        if (item == NULL) { return ESP_ERR_NOT_FOUND; }

        if (slot->header != 0) {
            data[0] = slot->header;
            size_t payload_cap = max_payload - 1;
            size_t copy_len = (item_size > payload_cap) ? payload_cap : item_size;
            if (copy_len > 0) {
                memcpy(&data[1], item, copy_len);
            }
            *len = copy_len + 1;
        } else {
            size_t copy_len = (item_size > max_payload) ? max_payload : item_size;
            if (copy_len > 0) {
                memcpy(data, item, copy_len);
            }
            *len = copy_len;
        }

        vRingbufferReturnItem(slot->tx_buff, item);

        if (slot->header != 0 && item_size > (max_payload - 1)) {
            ESP_LOGW(TAG, "NOSPLIT item truncated from %zu to %zu bytes (MTU/header limit)", item_size, max_payload - 1);
        } else if (slot->header == 0 && item_size > max_payload) {
            ESP_LOGW(TAG, "NOSPLIT item truncated from %zu to %zu bytes (MTU limit)", item_size, max_payload);
        }
        return ESP_OK;
    }
    else if (slot->buff_type == RINGBUF_TYPE_BYTEBUF) {
        if (!slot->auto_pack || slot->item_size == 0) {
            ESP_LOGE(TAG, "BYTEBUF lacks auto_pack config or item_size is 0");
            return ESP_ERR_INVALID_STATE;
        }

        size_t space_for_structs = max_payload;
        size_t start_offset = 0;
        if (slot->header != 0) {
            space_for_structs = max_payload - 1;
            start_offset = 1;
        }

        UBaseType_t bytes_waiting = 0;
        vRingbufferGetInfo(slot->tx_buff, NULL, NULL, NULL, NULL, &bytes_waiting);
        if (bytes_waiting == 0) {
            return ESP_ERR_NOT_FOUND;
        }

        size_t available_bytes = bytes_waiting;
        if (available_bytes > space_for_structs) {
            available_bytes = space_for_structs;
        }

        size_t max_structs = available_bytes / slot->item_size;
        if (max_structs == 0) {
            return ESP_ERR_NOT_FOUND;
        }

        size_t bytes_to_pull = max_structs * slot->item_size;
        size_t current_offset = start_offset;

        while ((current_offset - start_offset) < bytes_to_pull) {
            size_t recv_size = 0;
            size_t space_left = bytes_to_pull - (current_offset - start_offset);
            void *item = xRingbufferReceiveUpTo(slot->tx_buff, &recv_size, 0, space_left);
            if (item == NULL) {
                break;
            }
            memcpy(&data[current_offset], item, recv_size);
            current_offset += recv_size;
            vRingbufferReturnItem(slot->tx_buff, item);
        }

        if (slot->header != 0) {
            data[0] = slot->header;
        }
        *len = current_offset;
        return ESP_OK;
    }

    return ESP_ERR_INVALID_ARG;
}

static sys_ble_char_node_t *find_char_by_uuid(uint16_t char_uuid) {
    sys_ble_svc_node_t *s;
    LL_FOREACH(g_ble_ctx.services, s) {
        sys_ble_char_node_t *ch;
        LL_FOREACH(s->chars, ch) {
            if (ch->cfg.uuid == char_uuid) {
                return ch;
            }
        }
    }
    return NULL;
}

static sys_ble_svc_node_t *find_svc_by_uuid(uint16_t svc_uuid) {
    sys_ble_svc_node_t *s;
    LL_FOREACH(g_ble_ctx.services, s) {
        if (s->cfg.uuid == svc_uuid) {
            return s;
        }
    }
    return NULL;
}

static struct ble_gatt_chr_def *compile_chars(const sys_ble_char_node_t *chars_head, bool *out_success) {
    uint16_t num_chars = 0;
    const sys_ble_char_node_t *c;
    LL_FOREACH(chars_head, c) {
        num_chars++;
    }
    
    struct ble_gatt_chr_def *chrs = calloc(num_chars + 1, sizeof(struct ble_gatt_chr_def));
    if (!chrs) {
        *out_success = false;
        return NULL;
    }
    
    int c_idx = 0;
    LL_FOREACH(chars_head, c) {
        chrs[c_idx].uuid = malloc_uuid(c->cfg.uuid);
        if (!chrs[c_idx].uuid) {
            goto fail;
        }
        chrs[c_idx].access_cb = sys_ble_gatt_access_cb;
        chrs[c_idx].arg = (void *)c;
        chrs[c_idx].val_handle = (uint16_t *)&c->val_handle;
        
        uint16_t flags = 0;
        if (c->cfg.is_read)      flags |= BLE_GATT_CHR_F_READ;
        if (c->cfg.is_write)     flags |= BLE_GATT_CHR_F_WRITE;
        if (c->cfg.is_indicate)  flags |= BLE_GATT_CHR_F_INDICATE;
        if (c->cfg.is_notify)    flags |= BLE_GATT_CHR_F_NOTIFY;
        chrs[c_idx].flags = flags;
        
        if (c->cfg.desc) {
            struct ble_gatt_dsc_def *dscs = calloc(2, sizeof(struct ble_gatt_dsc_def));
            if (!dscs) {
                goto fail;
            }
            dscs[0].uuid = BLE_UUID16_DECLARE(0x2901);
            dscs[0].att_flags = BLE_ATT_F_READ;
            dscs[0].access_cb = sys_ble_gatt_dsc_cb;
            dscs[0].arg = (void *)c->cfg.desc;
            chrs[c_idx].descriptors = dscs;
        }
        c_idx++;
    }
    
    *out_success = true;
    return chrs;

fail:
    for (int i = 0; i <= c_idx; i++) {
        if (chrs[i].uuid) {
            free((void *)chrs[i].uuid);
        }
        if (chrs[i].descriptors) {
            free((void *)chrs[i].descriptors);
        }
    }
    free(chrs);
    *out_success = false;
    return NULL;
}

static esp_err_t populate_svc_def(struct ble_gatt_svc_def *svc_def, const sys_ble_svc_node_t *s) {
    bool success = false;
    struct ble_gatt_chr_def *chrs = compile_chars(s->chars, &success);
    if (!success && s->chars != NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    svc_def->type = s->cfg.is_primary ? BLE_GATT_SVC_TYPE_PRIMARY : BLE_GATT_SVC_TYPE_SECONDARY;
    svc_def->uuid = malloc_uuid(s->cfg.uuid);
    if (!svc_def->uuid) {
        if (chrs) {
            for (int j = 0; chrs[j].uuid != NULL; j++) {
                free((void *)chrs[j].uuid);
                if (chrs[j].descriptors) {
                    free((void *)chrs[j].descriptors);
                }
            }
            free(chrs);
        }
        return ESP_ERR_NO_MEM;
    }
    svc_def->characteristics = chrs;
    return ESP_OK;
}
