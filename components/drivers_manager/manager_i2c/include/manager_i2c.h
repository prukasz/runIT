#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "status.h"

#define M_I2C_MAX_DEVICES 16

// The struct pushed to the queue representing a waiting Driver Task
typedef struct {
    uint8_t driver_id;
    EventBits_t commnad;
    bool is_periodic;         // true if this driver should be re-added to the next scan cycle
    bool* keep_alive;         // Pointer to boolean. Manager drops the task if this is false.
} m_i2c_driver_job_t;

// The Configuration Structure for a Bus Manager
typedef struct {
    QueueHandle_t queue;
    i2c_master_bus_config_t bus_cfg;
    i2c_master_bus_handle_t bus;
    TaskHandle_t manager_task;
    EventGroupHandle_t events_to_wait;
    EventGroupHandle_t events_to_set;
    EventBits_t bits_to_wait;
    EventBits_t bits_to_set;
} m_i2c_config_t;

/**
 * @brief Initialize the Dual I2C Bus Managers
 */
status_err_report_t m_i2c_init(
    QueueHandle_t i2c_bus0_queue, gpio_num_t sda_0, gpio_num_t scl_0, uint8_t priority_bus0,
    EventGroupHandle_t wait_group_0, EventGroupHandle_t set_group_0, EventBits_t wait_bits_0, EventBits_t set_bits_0,
    
    QueueHandle_t i2c_bus1_queue, gpio_num_t sda_1, gpio_num_t scl_1, uint8_t priority_bus1,
    EventGroupHandle_t wait_group_1, EventGroupHandle_t set_group_1, EventBits_t wait_bits_1, EventBits_t set_bits_1
);


i2c_master_bus_handle_t m_i2c_get_bus_handle(uint8_t bus_num);
TaskHandle_t m_i2c_get_manager_task(uint8_t bus_num);

status_err_report_t m_i2c_add_driver(
    bool bus, 
    i2c_device_config_t dev_config, 
    TaskFunction_t task_func, 
    const char* task_name, 
    uint32_t stack_depth, 
    uint8_t priority, 
    bool is_periodic,
    uint8_t* out_id
);

i2c_master_dev_handle_t m_i2c_get_dev_handle_by_id(uint8_t id);
bool m_i2c_get_id_by_address(bool bus, uint16_t device_address, uint8_t* out_id);
bool m_i2c_set_active_state(uint8_t id, bool new_state);

