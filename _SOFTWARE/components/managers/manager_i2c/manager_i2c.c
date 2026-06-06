    #include "manager_i2c.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "rtos_utils.h"

#define TAG __FILE_NAME__
#define I2C_MANAGER_TASK_STACK_SIZE 4096
#define M_I2C_QUEUE_SIZE_PERIODIC 10
#define M_I2C_QUEUE_SIZE_APERIODIC 20

/*********Private handles ************************/
static m_i2c_config_t *manager_bus_cfg_0;
static m_i2c_config_t *manager_bus_cfg_1;
static i2c_master_bus_handle_t bus_handle_0 = NULL;
static i2c_master_bus_handle_t bus_handle_1 = NULL;
/*********Private handles ************************/

/********Static defines *************************************/
R_TASK_DEFINE(i2c_manager_task_0, I2C_MANAGER_TASK_STACK_SIZE);
R_TASK_DEFINE(i2c_manager_task_1, I2C_MANAGER_TASK_STACK_SIZE);
R_QUEUE_DEFINE(bus_periodic_queue_0, M_I2C_QUEUE_SIZE_PERIODIC, sizeof(m_i2c_driver_job_t));
R_QUEUE_DEFINE(bus_periodic_queue_1, M_I2C_QUEUE_SIZE_PERIODIC, sizeof(m_i2c_driver_job_t));
R_QUEUE_DEFINE(bus_aperiodic_queue_0, M_I2C_QUEUE_SIZE_APERIODIC, sizeof(m_i2c_driver_job_t));
R_QUEUE_DEFINE(bus_aperiodic_queue_1, M_I2C_QUEUE_SIZE_APERIODIC, sizeof(m_i2c_driver_job_t));
/********Static defines *************************************/


/********Internal driver instance ************************/
/**
 * @brief representation of one i2c driver possesing task
 * @param driver_task_handle created task of driver
 * @param master_dev_handle i2c driver handle affter addition to bus
 * @param dev_handle handle of whole driver device 
 * @param id assigned by manager
 * @param i2c_address current address of device
 * @param is_active -> should task be activated
 * @param bus i2c number 
 */
typedef struct{
    TaskHandle_t driver_task_handle;
    i2c_master_dev_handle_t master_dev_handle;
    void *dev_handle; 
    uint8_t id;
    uint8_t i2c_address; 
    bool is_active;
    bool bus;
}m_i2c_driver_slot;

static m_i2c_driver_slot driver_registry[M_I2C_MAX_DEVICES];
/********Internal driver instance ************************/

i2c_master_dev_handle_t m_i2c_get_master_dev_handle_by_id(uint8_t id) {
    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) { 

        if (driver_registry[i].is_active && driver_registry[i].id == id) {
            return driver_registry[i].master_dev_handle;
        }
    }
    return NULL;
}

TaskHandle_t m_i2c_get_driver_task(uint8_t id){
    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) {
        
        if (driver_registry[i].is_active && driver_registry[i].id == id) {
            return driver_registry[i].driver_task_handle;
        }
    }
    return NULL;
}


bool m_i2c_get_driver_id_by_address(bool bus, uint16_t device_address, uint8_t* out_id) {
    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) {
        if (driver_registry[i].is_active && 
            driver_registry[i].bus == bus && 
            driver_registry[i].i2c_address == device_address) 
        {
            if (out_id != NULL) {
                *out_id = driver_registry[i].id;
            }
            return true;
            
        }
    }
    return false; 
}

bool m_i2c_set_driver_state(uint8_t id, bool new_state) {
    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) {
        if (driver_registry[i].id == id) {
            driver_registry[i].is_active = new_state;
            return true; // State changed successfully
        }
    }
    return false; 
}

static void i2c_manager_task_function(void* params) {
    m_i2c_config_t* manager = (m_i2c_config_t*)params;
    QueueHandle_t j_periodic;
    QueueHandle_t j_aperiodic;
    TaskHandle_t manager_task_handle = xTaskGetCurrentTaskHandle();
    if (manager->bus_cfg.i2c_port == I2C_NUM_0) {
        j_periodic = bus_periodic_queue_0;
        j_aperiodic = bus_aperiodic_queue_0;
    }else{
        j_periodic = bus_periodic_queue_1;
        j_aperiodic = bus_aperiodic_queue_1;
    }
    uint32_t bus_owner = (manager->bus_cfg.i2c_port == I2C_NUM_0) ? OWNER_I2C_BUS_0 : OWNER_I2C_BUS_0;
    while (1) {
        R_EVENT_AWAIT_ANY(manager->m_i2c_events, manager->m_i2c_bit_queue_process, WAIT_FOREVER); 
        uint32_t items_in_aperiodic = uxQueueMessagesWaiting(j_aperiodic);
        for (UBaseType_t i = 0; i < items_in_aperiodic; i++) {
            m_i2c_driver_job_t current_job;
            
            if (R_QUEUE_RECEIVE(j_aperiodic, &current_job, NO_WAIT) == pdTRUE) {
                
                uint32_t notify_value = 0;
                TaskHandle_t driver_task = m_i2c_get_driver_task(current_job.driver_id);
                if (current_job.keep_alive){
                    if (driver_task == NULL) { 
                        ESP_LOGW(TAG, "Driver task %d not found during aperiodic cycle", current_job.driver_id);
                    } else {
                        R_NOTIFY_SEND(driver_task, (uint32_t)manager_task_handle);
                        if (!IS_OK(R_NOTIFY_AWAIT(MSEC(50), &notify_value))) {
                            ESP_LOGE(TAG, "Driver task %d on bus %d failed to respond in time", current_job.driver_id, manager->bus_cfg.i2c_port);
                            STA_P(STA_C(ERR_I2C_TIMEOUT, bus_owner , current_job.driver_id));
                        }else if (notify_value != 0){
                            ESP_LOGE(TAG, "Driver task %d on bus %d failed to update", current_job.driver_id, manager->bus_cfg.i2c_port);
                            STA_P(STA_C(ERR_I2C_TRANSMISSION_FAILURE, bus_owner , current_job.driver_id));
                        }
                    } 
                }
            }
        }

        uint32_t items_in_periodic = uxQueueMessagesWaiting(j_periodic);
        for (UBaseType_t i = 0; i < items_in_periodic; i++) {
            m_i2c_driver_job_t current_job;

            if (R_QUEUE_RECEIVE(j_periodic, &current_job, NO_WAIT) == pdTRUE) {
                
                uint32_t notify_value = 0;
                if (current_job.keep_alive){
                    TaskHandle_t driver_task = m_i2c_get_driver_task(current_job.driver_id);
                    if (driver_task == NULL) {
                        ESP_LOGW(TAG, "Driver task %d not found during periodic cycle", current_job.driver_id);
                    }else{
                        R_NOTIFY_SEND(driver_task, (uint32_t)manager_task_handle);
                        if (!IS_OK(R_NOTIFY_AWAIT(MSEC(50), &notify_value))) {
                            ESP_LOGE(TAG, "Driver task %d on bus %d failed to respond in time", current_job.driver_id, manager->bus_cfg.i2c_port);
                            STA_P(STA_C(ERR_I2C_TIMEOUT, bus_owner , current_job.driver_id));
                        }else if (notify_value != 0){
                            ESP_LOGE(TAG, "Driver task %d on bus %d failed to update", current_job.driver_id, manager->bus_cfg.i2c_port);
                            STA_P(STA_C(ERR_I2C_TRANSMISSION_FAILURE, bus_owner , current_job.driver_id));
                        }
                    }
                   
                } 
                R_QUEUE_SEND(j_periodic, &current_job, NO_WAIT);           
            }
        }
        R_EVENT_SET(manager->m_i2c_events, manager->m_i2c_bit_queue_done);
         R_NOTIFY_SEND(manager->supervisor_task_handle, 0);
    }
 
}
#undef OWNER
#define OWNER OWNER_I2C_BUS_0
status_rep_t m_i2c_init(m_i2c_config_t* bus0_config, m_i2c_config_t* bus1_config) 
{
    manager_bus_cfg_0 = bus0_config;
    manager_bus_cfg_1 = bus1_config;
    manager_bus_cfg_0->manager_task_handle = i2c_manager_task_0;
    manager_bus_cfg_1->manager_task_handle = i2c_manager_task_1;
    
    CHECK_ESP_CALL_R(i2c_new_master_bus(&manager_bus_cfg_0->bus_cfg, &bus_handle_0));

#undef OWNER
#define OWNER OWNER_I2C_BUS_1

    CHECK_ESP_CALL_R(i2c_new_master_bus(&manager_bus_cfg_1->bus_cfg, &bus_handle_1));

    R_TASK_START_ON_CORE(i2c_manager_task_0, &i2c_manager_task_function, bus0_config, CONFIG_PRIORITY_I2C_MANAGER_TASK, 0);
    R_TASK_START_ON_CORE(i2c_manager_task_1, &i2c_manager_task_function, bus1_config, CONFIG_PRIORITY_I2C_MANAGER_TASK, 1);
    
    ESP_LOGI(TAG, "I2C Manager initialized successfully on both buses");

    return STA_OK;
}

i2c_master_bus_handle_t m_i2c_get_bus_handle(uint8_t bus_num) {
    return (bus_num == 0) ? bus_handle_0 : bus_handle_1;
}

TaskHandle_t m_i2c_get_manager_task(uint8_t bus_num) {
    return (bus_num == 0) ? manager_bus_cfg_0->manager_task_handle : manager_bus_cfg_1->manager_task_handle;
}


static uint8_t _next_driver_id = 1;

#undef OWNER
#define OWNER OWNER_I2C_ADD_DRIVER
status_rep_t m_i2c_add_driver(
    bool bus, 
    i2c_device_config_t dev_config, 
    i2c_master_dev_handle_t* out_master_dev_handle,
    void* dev_handle,
    TaskHandle_t task_func,
    bool is_periodic,
    uint8_t* out_id
) 
{
    CHECK_HANDLE_R(out_master_dev_handle);
    CHECK_NOT_NULL_R(task_func);
    CHECK_NOT_NULL_R(out_id);
    CHECK_NOT_NULL_R(dev_handle);

    int slot = -1;
    uint8_t i2c_addr = dev_config.device_address & 0x7F; // Mask to 7 bits for registry storage
    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) {
        if (driver_registry[i].i2c_address == i2c_addr && driver_registry[i].bus == bus) {
            ESP_LOGE(TAG, "I2C address conflict detected for address 0x%02X on bus %d with existing driver ID %d", i2c_addr, bus ? 1 : 0, driver_registry[i].id);   
            STA_RP(STA_C(ERR_I2C_DEV_ADDR_CONFLICT, OWNER_I2C_ADD_DRIVER, dev_config.device_address));
        }
    }

    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) {
        if (!driver_registry[i].is_active && driver_registry[i].id == 0) { 
            // id == 0 means completely uninitialized
            slot = i;
            break;
        };
    }

    if (slot == -1) {
        ESP_LOGE(TAG, "Driver registry full. Cannot add driver with address 0x%02X on bus %d", i2c_addr, bus ? 1 : 0);
        STA_RP(STA_C(ERR_I2C_DEV_REG_FULL, OWNER_I2C_ADD_DRIVER, M_I2C_MAX_DEVICES));
    }

    i2c_master_bus_handle_t target_bus_handle = m_i2c_get_bus_handle(bus ? 1 : 0);
    i2c_master_dev_handle_t new_dev_handle;
    
    CHECK_ESP_CALL_R(i2c_master_bus_add_device(target_bus_handle, &dev_config, &new_dev_handle));
    
    driver_registry[slot].i2c_address = i2c_addr;
    driver_registry[slot].master_dev_handle = new_dev_handle;
    driver_registry[slot].bus = bus;
    driver_registry[slot].id = _next_driver_id++;
    driver_registry[slot].is_active = true;
    driver_registry[slot].dev_handle = dev_handle;
    *out_master_dev_handle = new_dev_handle;

    // in case of periodic operation
    if (out_id) *out_id = driver_registry[slot].id;
    driver_registry[slot].driver_task_handle = task_func;

    ESP_LOGI(TAG, "m_i2c_add_driver: registered driver id=%d task_handle=%p dev_handle=%p", driver_registry[slot].id, (void*)driver_registry[slot].driver_task_handle, driver_registry[slot].dev_handle);
    if (is_periodic) {
        m_i2c_driver_job_t initial_job = {
            .driver_id = driver_registry[slot].id,
            .is_periodic = true,
            .keep_alive = &driver_registry[slot].is_active //from dev 
        };
        
        QueueHandle_t target_queue = (bus == 0) ? bus_periodic_queue_0 : bus_periodic_queue_1;
        R_QUEUE_SEND(target_queue, &initial_job, WAIT_FOREVER);
    }
    
    ESP_LOGI(TAG, "Driver added with ID %d for I2C address 0x%02X on bus %d", driver_registry[slot].id, i2c_addr, bus ? 1 : 0);
    return STA_OK;
}


status_rep_t m_i2c_enqueue_aperiodic_job(uint8_t id) {
    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) {
        if (driver_registry[i].is_active && driver_registry[i].id == id) {
            m_i2c_driver_job_t job = {
                .driver_id = id,
                .is_periodic = false,
                .keep_alive = &driver_registry[i].is_active
            };
            QueueHandle_t target_queue = (driver_registry[i].bus == 0) ? bus_aperiodic_queue_0 : bus_aperiodic_queue_1;
            R_QUEUE_SEND(target_queue, &job, NO_WAIT);
            return STA_OK;
        }
    }
    ESP_LOGE(TAG, "Failed to enqueue aperiodic job: No active driver with ID %d", id);
    STA_RP(STA_C(ERR_I2C_DEV_NOT_FOUND, OWNER_I2C_ADD_DRIVER, id));
}

void* m_i2c_get_dev_handle(uint8_t id) {
    for (int i = 0; i < M_I2C_MAX_DEVICES; i++) {
        if (driver_registry[i].is_active && driver_registry[i].id == id) {
            return driver_registry[i].dev_handle;
        }
    }
    return NULL; 
}

#undef OWNER
#define OWNER OWNER_I2C_ADD_DRIVER
status_rep_t m_i2c_device_present(bool bus, uint8_t device_address) {
    ESP_LOGI(TAG, "Probing for device at address 0x%02X on bus %d", device_address, bus ? 1 : 0);
    CHECK_ESP_CALL_R(i2c_master_probe(m_i2c_get_bus_handle(bus ? 1 : 0), device_address, 100));
    return STA_OK;
}