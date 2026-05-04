#include "sdkconfig.h"
#include "manager_i2c.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#define TAG __FILE_NAME__

/* Private variables */
static m_i2c_config_t *manager_bus_cfg_0;
static m_i2c_config_t *manager_bus_cfg_1;

static TaskHandle_t manager_task_handle_0 = NULL;
static TaskHandle_t manager_task_handle_1 = NULL;

static i2c_master_bus_handle_t bus_handle_0 = NULL;
static i2c_master_bus_handle_t bus_handle_1 = NULL;

static QueueHandle_t bus_periodic_queue_0 = NULL;
static QueueHandle_t bus_periodic_queue_1 = NULL;
static QueueHandle_t bus_aperiodic_queue_0 = NULL;
static QueueHandle_t bus_aperiodic_queue_1 = NULL;
/* Private variables */

/* Driver slots */
/**
 * @brief This struct holds one "device" represented by task 
 * @param driver_id: the ID of the driver/device
 * @param is_periodic: whether this device should be re-added to the periodic queue after processing
 * @param keep_alive: pointer to a boolean. If false, manager will not notify the task and will remove it from the queue (used for emergency stop or if task is deleted)
 * @param task_handle: the TaskHandle_t of the driver task to notify "do your job"
 */
typedef struct{
    TaskHandle_t task_handle;
    i2c_master_dev_handle_t dev_handle;
    uint8_t id;
    uint8_t i2_address; 
    bool is_active;
    bool bus;
}m_i2c_driver_slot;

static m_i2c_driver_slot driver_registry[M_I2C_MAX_DEVICES];
/* Driver slots */


i2c_master_dev_handle_t m_i2c_get_dev_handle_by_id(uint8_t id) {
    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) {
        
        if (driver_registry[i].is_active && driver_registry[i].id == id) {
            return driver_registry[i].dev_handle;
        }
    }
    return NULL;
}

TaskHandle_t m_i2c_get_dev_task(uint8_t id){
    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) {
        
        if (driver_registry[i].is_active && driver_registry[i].id == id) {
            return driver_registry[i].task_handle;
        }
    }
    return NULL;
}

bool m_i2c_get_id_by_address(bool bus, uint16_t device_address, uint8_t* out_id) {
    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) {
        if (driver_registry[i].is_active && 
            driver_registry[i].bus == bus && 
            driver_registry[i].i2_address == device_address) 
        {
            if (out_id != NULL) {
                *out_id = driver_registry[i].id;
            }
            return true;
            
        }
    }
    return false; 
}

bool m_i2c_set_active_state(uint8_t id, bool new_state) {
    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) {
        if (driver_registry[i].id == id) {
            driver_registry[i].is_active = new_state;
            return true; // State changed successfully
        }
    }
    return false; 
}


static void i2c_manager_task(void* params) {
    m_i2c_config_t* manager = (m_i2c_config_t*)params;
    QueueHandle_t j_periodic;
    QueueHandle_t j_aperiodic;
    TaskHandle_t manager_task_handle = xTaskGetCurrentTaskHandle();
    if (manager->m_i2c_bus_num == 0) {
        j_periodic = bus_periodic_queue_0;
        j_aperiodic = bus_aperiodic_queue_0;
    }else{
        j_periodic = bus_periodic_queue_1;
        j_aperiodic = bus_aperiodic_queue_1;
    }
    while (1) {
        xEventGroupWaitBits(manager->m_i2c_events, manager->m_i2c_bits_queue_process, pdTRUE, pdFALSE, portMAX_DELAY);
        
        uint32_t items_in_aperiodic = uxQueueMessagesWaiting(j_aperiodic);
        for (UBaseType_t i = 0; i < items_in_aperiodic; i++) {
            m_i2c_driver_job_t current_job;
            
            if (xQueueReceive(j_aperiodic, &current_job, 0) == pdTRUE) {
                

                if (current_job.keep_alive){
                    TaskHandle_t driver_task = m_i2c_get_dev_task(current_job.driver_id);
                    xTaskNotify(driver_task, (uint32_t)manager_task_handle, eSetValueWithOverwrite);
                    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50)) != pdTRUE) {
                        xEventGroupSetBits(manager->m_i2c_events, manager->m_i2c_bits_queue_timeout);
                    }
                }
            }
        }
        uint32_t items_in_periodic = uxQueueMessagesWaiting(j_periodic);
        for (UBaseType_t i = 0; i < items_in_periodic; i++) {
            m_i2c_driver_job_t current_job;
            
            if (xQueueReceive(j_periodic, &current_job, 0) == pdTRUE) {
                

                if (current_job.keep_alive){
                    TaskHandle_t driver_task = m_i2c_get_dev_task(current_job.driver_id);
                    xTaskNotify(driver_task, (uint32_t)manager_task_handle , eSetValueWithOverwrite);
                    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(50)) != pdTRUE) {
                        xEventGroupSetBits(manager->m_i2c_events, manager->m_i2c_bits_queue_timeout);
                    }
                    
                }
                xQueueSend(j_periodic, &current_job, 0);
            }
        }
        xEventGroupSetBits(manager->m_i2c_events, manager->m_i2c_bits_queue_done);
        portYIELD();
    }
}

status_err_report_t m_i2c_init(m_i2c_config_t* bus0_config, m_i2c_config_t* bus1_config) 
{
    esp_err_t err;
    manager_bus_cfg_0 = bus0_config;
    manager_bus_cfg_1 = bus1_config;
    bus_periodic_queue_0 = xQueueCreate(manager_bus_cfg_0->queue_size_periodic, sizeof(m_i2c_driver_job_t));
    bus_periodic_queue_1 = xQueueCreate(manager_bus_cfg_1->queue_size_periodic, sizeof(m_i2c_driver_job_t));
    bus_aperiodic_queue_0 = xQueueCreate(manager_bus_cfg_0->queue_size_aperiodic, sizeof(m_i2c_driver_job_t));
    bus_aperiodic_queue_1 = xQueueCreate(manager_bus_cfg_1->queue_size_aperiodic, sizeof(m_i2c_driver_job_t));

    if (!bus_periodic_queue_0 || !bus_periodic_queue_1 || !bus_aperiodic_queue_0 || !bus_aperiodic_queue_1) {
        STA_RET_PUSH_LOG(STA_C(ERR_ESP_ERR_NO_MEM, OWN_m_i2c_init, 0), "Failed to create queues");
    }

    err = i2c_new_master_bus(&manager_bus_cfg_0->bus_cfg, &bus_handle_0);
    if (err != ESP_OK) { STA_RET_PUSH_LOG(STA_C((uint16_t)err, OWN_m_i2c_init, 0),
         "Failed to initialize I2C Bus 0 sda %d, scl %d", manager_bus_cfg_0->bus_cfg.sda_io_num, manager_bus_cfg_0->bus_cfg.scl_io_num); }

    
    err = i2c_new_master_bus(&manager_bus_cfg_1->bus_cfg, &bus_handle_1);
    if (err != ESP_OK) { STA_RET_PUSH_LOG(STA_C((uint16_t)err, OWN_m_i2c_init, 0),
         "Failed to initialize I2C Bus 1 sda %d, scl %d", manager_bus_cfg_1->bus_cfg.sda_io_num, manager_bus_cfg_1->bus_cfg.scl_io_num); }

    // --- Create Manager Tasks ---
    if (xTaskCreatePinnedToCore(i2c_manager_task, "i2c_mgr_0", manager_bus_cfg_0->task_stack_size, manager_bus_cfg_0, manager_bus_cfg_0->task_priority, &manager_task_handle_0, 1) != pdPASS) {
        STA_RET_PUSH_LOG(STA_C(ERR_ESP_ERR_NO_MEM, OWN_m_i2c_init, 0), "Failed to create manager task for bus 0");
    }
    if (xTaskCreatePinnedToCore(i2c_manager_task, "i2c_mgr_1", manager_bus_cfg_1->task_stack_size, manager_bus_cfg_1, manager_bus_cfg_1->task_priority, &manager_task_handle_1, 0) != pdPASS) {
        STA_RET_PUSH_LOG(STA_C(ERR_ESP_ERR_NO_MEM, OWN_m_i2c_init, 0), "Failed to create manager task for bus 1");
    }
    STA_RET_PUSH_LOG(STA_I(STA_I2C_INITILAIZED, OWN_m_i2c_init, 0), "I2C Manager initialized successfully");
    return STA_OK;
}

i2c_master_bus_handle_t m_i2c_get_bus_handle(uint8_t bus_num) {
    return (bus_num == 0) ? bus_handle_0 : bus_handle_1;
}

TaskHandle_t m_i2c_get_manager_task(uint8_t bus_num) {
    return (bus_num == 0) ? manager_task_handle_0 : manager_task_handle_1;
}


static uint8_t _next_driver_id = 1;

status_err_report_t m_i2c_add_driver(
    bool bus, 
    i2c_device_config_t dev_config, 
    TaskHandle_t task_func,
    bool is_periodic,
    uint8_t* out_id
) 
{
    esp_err_t err;

    int slot = -1;
    uint8_t i2c_addr = dev_config.device_address & 0x7F; // Mask to 7 bits for registry storage
    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) {
        if (driver_registry[i].i2_address == i2c_addr && driver_registry[i].bus == bus) {
            STA_RET_PUSH_LOG(STA_C(ERR_I2C_DEV_ADDR_CONFLICT, OWN_m_i2c_add_driver, dev_config.device_address), 
                "Failed to add I2C driver: address conflict at 0x%02X", dev_config.device_address);
        }
    }

    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) {
        if (!driver_registry[i].is_active && driver_registry[i].id == 0) { 
            // id == 0 means completely uninitialized
            slot = i;
            break;
        };
    }

    if (slot == -1) {STA_RET_PUSH_LOG(STA_C(ERR_I2C_DEV_REG_FULL, OWN_m_i2c_add_driver, M_I2C_MAX_DEVICES), "Failed to add I2C driver: registry full");}

    i2c_master_bus_handle_t target_bus_handle = m_i2c_get_bus_handle(bus ? 1 : 0);
    i2c_master_dev_handle_t new_dev_handle;
    
    err = i2c_master_bus_add_device(target_bus_handle, &dev_config, &new_dev_handle);
    if (err != ESP_OK) {STA_RET_PUSH_LOG(STA_C((uint16_t)err, OWN_m_i2c_add_driver, dev_config.device_address), "Failed to add I2C device to bus %d: %02X", bus ? 1 : 0, dev_config.device_address);}

    driver_registry[slot].i2_address = i2c_addr;
    driver_registry[slot].dev_handle = new_dev_handle;
    driver_registry[slot].bus = bus;
    driver_registry[slot].id = _next_driver_id++;
    driver_registry[slot].is_active = true;

    // in case of periodic operation
    if (out_id) *out_id = driver_registry[slot].id;
    driver_registry[slot].task_handle = task_func;
    if (is_periodic) {
        m_i2c_driver_job_t initial_job = {
            .driver_id = driver_registry[slot].id,
            .is_periodic = true,
            .keep_alive = &driver_registry[slot].is_active //from dev 
        };
        
        QueueHandle_t target_queue = (bus == 0) ? bus_periodic_queue_0 : bus_periodic_queue_1;
        xQueueSend(target_queue, &initial_job, portMAX_DELAY);
    }
    
    STA_RET_PUSH_LOG(STA_I(STA_I2C_DRIVER_ADDED, OWN_m_i2c_add_driver, dev_config.device_address), "Driver added to bus %d with address 0x%02X", bus ? 1 : 0, dev_config.device_address);
    return STA_OK;
}
status_err_report_t m_i2c_enqueue_aperiodic_job(uint8_t id) {
    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) {
        if (driver_registry[i].is_active && driver_registry[i].id == id) {
            m_i2c_driver_job_t job = {
                .driver_id = id,
                .is_periodic = false,
                .keep_alive = &driver_registry[i].is_active
            };
            QueueHandle_t target_queue = (driver_registry[i].bus == 0) ? bus_aperiodic_queue_0 : bus_aperiodic_queue_1;
            xQueueSend(target_queue, &job, 0);
            return STA_OK;
        }
    }
    STA_RET_PUSH_LOG(STA_E(ERR_I2C_DEV_NOT_FOUND, OWN_m_i2c_add_driver, id), "Failed to enqueue aperiodic job: driver with ID %d not found", id);    
}
