
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
    bool is_periodic;         // true if this driver should be re-added to the next scan cycle
    bool* keep_alive;         // Pointer to boolean. Manager drops the task if this is false.
} m_i2c_driver_job_t;

// The Configuration Structure for a Bus Manager
typedef struct {
    EventGroupHandle_t m_i2c_events;
    EventBits_t m_i2c_bits_queue_process;
    EventBits_t m_i2c_bits_queue_done;
    EventBits_t m_i2c_bits_queue_timeout;
    EventBits_t m_i2c_bits_emergency_stop;
    i2c_master_bus_config_t bus_cfg;
    uint8_t m_i2c_bus_num;
    uint8_t task_priority;
    uint32_t task_stack_size;
    bool core;
    uint8_t queue_size_aperiodic;
    uint8_t queue_size_periodic;
} m_i2c_config_t;

/**
 * @brief Initialize the Dual I2C Bus Managers
 */
status_rep_t m_i2c_init(m_i2c_config_t* bus0_config, m_i2c_config_t* bus1_config);


status_rep_t m_i2c_add_driver(
    bool bus, 
    i2c_device_config_t dev_config, 
    i2c_master_dev_handle_t* master_dev_handle,
    void* dev_handle,
    TaskHandle_t task_func, 
    bool is_periodic,
    uint8_t* out_id
);

i2c_master_dev_handle_t m_i2c_get_master_dev_handle_by_id(uint8_t id);
bool m_i2c_get_id_by_address(bool bus, uint16_t device_address, uint8_t* out_id);
bool m_i2c_set_active_state(uint8_t id, bool new_state);
status_rep_t m_i2c_enqueue_aperiodic_job(uint8_t id);

esp_err_t m_i2c_device_present(bool bus_num, uint8_t i2c_addres);
void* m_i2c_get_dev_handle(uint8_t id);