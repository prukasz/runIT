#include "sdkconfig.h"
#include "manager_i2c.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#define TAG __FILE_NAME__

// Internal manager instances
static m_i2c_config_t manager_bus_0;
static m_i2c_config_t manager_bus_1;

typedef struct{
    TaskHandle_t task_handle;
    i2c_device_config_t dev_cfg;
    i2c_master_dev_handle_t dev_handle;
    uint8_t id;
    bool is_active;
    bool bus;
}m_i2c_driver_slot;

static m_i2c_driver_slot driver_registry[M_I2C_MAX_DEVICES];

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
            driver_registry[i].dev_cfg.device_address == device_address) 
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
    
    EventGroupHandle_t events_to_wait = manager->events_to_wait;
    EventGroupHandle_t events_to_set  = manager->events_to_set;
    EventBits_t bits_to_wait          = manager->bits_to_wait;
    EventBits_t bits_to_set           = manager->bits_to_set;
    QueueHandle_t jobs                = manager->queue;

    while (1) {
        // 1. Wait for the scan cycle to be triggered
        if (events_to_wait != NULL && bits_to_wait != 0) {
            xEventGroupWaitBits(events_to_wait, bits_to_wait, pdTRUE, pdFALSE, portMAX_DELAY);
        }

        // 2. Snapshot the queue to prevent infinite loops
        UBaseType_t items_in_cycle = uxQueueMessagesWaiting(jobs);

        for (UBaseType_t i = 0; i < items_in_cycle; i++) {
            m_i2c_driver_job_t current_job;
            
            if (xQueueReceive(jobs, &current_job, 0) == pdTRUE) {
                
                // --- NEW LOGIC: Check if we should execute or skip ---
                bool should_execute = true;
                
                // If it is periodic AND has a keep_alive switch, check the switch
                if (current_job.is_periodic && current_job.keep_alive != NULL) {
                    if (*(current_job.keep_alive) == false) {
                        should_execute = false; // The task is paused, skip execution
                    }
                }


                if (should_execute) {
                    // 3. WAKE UP THE DRIVER TASK (Give it the "Token")
                    TaskHandle_t driver_task = m_i2c_get_dev_task(current_job.driver_id);
                    xTaskNotify(driver_task, current_job.commnad, 0);
                    
                    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100)) == 0) {
                    }
                }

                //return task if task is periodic so next cycle ready
                if (current_job.is_periodic) {
                    xQueueSend(jobs, &current_job, 0);
                }
            }
        }

        // 6. Queue is empty for this cycle. Signal completion.
        if (events_to_set != NULL && bits_to_set != 0) {
            xEventGroupSetBits(events_to_set, bits_to_set);
        }
    }
}

status_err_report_t m_i2c_init(
    QueueHandle_t i2c_bus0_queue, gpio_num_t sda_0, gpio_num_t scl_0, uint8_t priority_bus0,
    EventGroupHandle_t wait_group_0, EventGroupHandle_t set_group_0, EventBits_t wait_bits_0, EventBits_t set_bits_0,
    
    QueueHandle_t i2c_bus1_queue, gpio_num_t sda_1, gpio_num_t scl_1, uint8_t priority_bus1,
    EventGroupHandle_t wait_group_1, EventGroupHandle_t set_group_1, EventBits_t wait_bits_1, EventBits_t set_bits_1) 
{
    esp_err_t err;

    // --- Bus 0 Init ---
    manager_bus_0.queue          = i2c_bus0_queue;
    manager_bus_0.events_to_wait = wait_group_0;
    manager_bus_0.events_to_set  = set_group_0;
    manager_bus_0.bits_to_wait   = wait_bits_0;
    manager_bus_0.bits_to_set    = set_bits_0;
    manager_bus_0.bus_cfg = (i2c_master_bus_config_t) {
        .i2c_port = I2C_NUM_0, .sda_io_num = sda_0, .scl_io_num = scl_0,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&manager_bus_0.bus_cfg, &manager_bus_0.bus);
    if (err != ESP_OK) { STA_ERR_RETURN_PUSH(STA_ERR_S_C((uint16_t)err, OWN_m_i2c_init)); }
    ESP_LOGI(TAG, "I2C Bus 0 initialized on SDA: %d, SCL: %d", sda_0, scl_0);
    // --- Bus 1 Init ---
    manager_bus_1.queue          = i2c_bus1_queue;
    manager_bus_1.events_to_wait = wait_group_1;
    manager_bus_1.events_to_set  = set_group_1;
    manager_bus_1.bits_to_wait   = wait_bits_1;
    manager_bus_1.bits_to_set    = set_bits_1;
    manager_bus_1.bus_cfg = (i2c_master_bus_config_t) {
        .i2c_port = I2C_NUM_1, .sda_io_num = sda_1, .scl_io_num = scl_1,
        .clk_source = I2C_CLK_SRC_DEFAULT, .glitch_ignore_cnt = 7, .flags.enable_internal_pullup = true,
    };
    err = i2c_new_master_bus(&manager_bus_1.bus_cfg, &manager_bus_1.bus);
    if (err != ESP_OK) { STA_ERR_RETURN_PUSH(STA_ERR_S_C((uint16_t)err, OWN_m_i2c_init)); }
    ESP_LOGI(TAG, "I2C Bus 1 initialized on SDA: %d, SCL: %d", sda_1, scl_1);

    // --- Create Manager Tasks ---
    if (xTaskCreatePinnedToCore(i2c_manager_task, "i2c_mgr_0", 8192, &manager_bus_0, priority_bus0, &manager_bus_0.manager_task, 1) != pdPASS) {
        STA_ERR_RETURN_PUSH(STA_ERR_S_C(ERR_ESP_ERR_NO_MEM, OWN_m_i2c_init));
    }
    if (xTaskCreatePinnedToCore(i2c_manager_task, "i2c_mgr_1", 8192, &manager_bus_1, priority_bus1, &manager_bus_1.manager_task, 0) != pdPASS) {
        STA_ERR_RETURN_PUSH(STA_ERR_S_C(ERR_ESP_ERR_NO_MEM, OWN_m_i2c_init));
    }
    ESP_LOGI(TAG, "I2C Manager tasks created for both buses");
    return STA_ERR_OK;
}

i2c_master_bus_handle_t m_i2c_get_bus_handle(uint8_t bus_num) {
    return (bus_num == 0) ? manager_bus_0.bus : manager_bus_1.bus;
}

TaskHandle_t m_i2c_get_manager_task(uint8_t bus_num) {
    return (bus_num == 0) ? manager_bus_0.manager_task : manager_bus_1.manager_task;
}

static uint8_t s_next_driver_id = 1;

status_err_report_t m_i2c_add_driver(
    bool bus, 
    i2c_device_config_t dev_config, 
    TaskFunction_t task_func, 
    const char* task_name, 
    uint32_t stack_depth, 
    uint8_t priority, 
    bool is_periodic,
    uint8_t* out_id
) 
{
    esp_err_t err;

    // 1. Find an empty slot in the registry
    int slot = -1;
    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) {
        if (!driver_registry[i].is_active && driver_registry[i].id == 0) { 
            // id == 0 means completely uninitialized
            slot = i;
            break;
        } else if (!driver_registry[i].is_active) {
            // Overwrite a previously deleted slot
            slot = i; 
            break;
        }
    }

    if (slot == -1) {STA_ERR_RETURN_PUSH(STA_ERR_S_C(ERR_I2C_DEV_REG_FULL, OWN_m_i2c_add_driver));}

    i2c_master_bus_handle_t target_bus_handle = m_i2c_get_bus_handle(bus ? 1 : 0);
    i2c_master_dev_handle_t new_dev_handle;
    
    err = i2c_master_bus_add_device(target_bus_handle, &dev_config, &new_dev_handle);
    if (err != ESP_OK) {STA_ERR_RETURN_PUSH(STA_ERR_S_C((uint16_t)err, OWN_m_i2c_add_driver));}

    driver_registry[slot].dev_cfg = dev_config;
    driver_registry[slot].dev_handle = new_dev_handle;
    driver_registry[slot].bus = bus;
    driver_registry[slot].id = s_next_driver_id++;
    driver_registry[slot].is_active = true;

    BaseType_t core_id = (bus == 0) ? 1 : 0;
    
    if (xTaskCreatePinnedToCore(task_func, task_name, stack_depth, (void*)driver_registry[slot].dev_handle, priority, &driver_registry[slot].task_handle, core_id) != pdPASS) {
        i2c_master_bus_rm_device(new_dev_handle);
        driver_registry[slot].is_active = false;
        STA_ERR_RETURN_PUSH(STA_ERR_S_C(ERR_I2C_DEV_ADD_FAIL, OWN_m_i2c_add_driver));
    }

    // in case of periodic operation
    *out_id = driver_registry[slot].id;
    if (is_periodic) {
        m_i2c_driver_job_t initial_job = {
            .driver_id = driver_registry[slot].id,
            .commnad = 1,
            .is_periodic = true,
            .keep_alive = &driver_registry[slot].is_active //from dev 
        };
        
        QueueHandle_t target_queue = (bus == 0) ? manager_bus_0.queue : manager_bus_1.queue;
        xQueueSend(target_queue, &initial_job, portMAX_DELAY);
    }
    ESP_LOGI(TAG, "Driver '%s' added! ID: %u, Addr: 0x%02X", task_name, driver_registry[slot].id, dev_config.device_address);
    return STA_ERR_OK;
}