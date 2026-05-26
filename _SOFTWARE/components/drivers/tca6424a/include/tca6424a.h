#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2c_master.h>

typedef enum {
    TCA_ON_RISING_EDGE = 0,
    TCA_ON_FALLING_EDGE =1,
    TCA_ON_CHANGE = 2, 
}tca_interrupt_mode_e;

typedef struct {
    i2c_master_dev_handle_t  i2c_dev_handle;
    i2c_device_config_t     i2c_dev_config;
    TaskHandle_t task_handle;
    uint8_t last_read_input[3];
    uint8_t output[3];
    uint8_t polarity_cfg[3];
    uint8_t config[3];

    struct {
        uint8_t p0_to_update           : 1;
        uint8_t p1_to_update           : 1;
        uint8_t p2_to_update           : 1;
        uint8_t cfg_to_update          : 1;
        uint8_t cfg_polarity_to_update : 1;
        uint8_t _reserved              : 3;
    } to_update;

    volatile bool interrupt_present;

    void (*callbacks[24])(void* arg);
    uint8_t pin_trigger_modes[24];
    void* callback_args[24];
}tca_data_t;

typedef tca_data_t* tca6424a_handle_t;

void tca_task(void* dev_handle);

void p_gpio_expander_intr_pin_callback(void* arg); 

esp_err_t tca_register_pin_callback(tca6424a_handle_t handle, uint32_t pin_mask, void (*cb)(void*), tca_interrupt_mode_e mode, void* arg);

esp_err_t tca_preset_pins(tca6424a_handle_t handle, uint32_t pins_mask, uint32_t pins_state, bool update_now);
esp_err_t tca_preset_cfg(tca6424a_handle_t handle, uint32_t cfg_mask, uint32_t cfg_state, bool update_now);
esp_err_t tca_preset_polarity(tca6424a_handle_t handle, uint32_t polarity_mask, uint32_t polarity_state, bool update_now);
esp_err_t tca_get_pin_level(tca6424a_handle_t handle, uint32_t *out_level, bool force_update);
uint32_t tca_get_pin_output(tca6424a_handle_t handle);
tca6424a_handle_t tca_new(uint8_t i2c_address);







