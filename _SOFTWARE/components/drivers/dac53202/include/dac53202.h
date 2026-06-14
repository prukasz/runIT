#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <driver/i2c_master.h>
#include <esp_err.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DAC53202_VREF_MV 3500

typedef struct _dac53202_data_t* dac53202_handle_t;

typedef struct _dac53202_data_t {
    i2c_device_config_t      i2c_device_config;  
    i2c_master_dev_handle_t  i2c_dev_handle;     
    TaskHandle_t             task_handle;        
    
    uint16_t                 common_config;      
    uint16_t                 channel_raw_value[2]; 
    
    struct {
        uint8_t update_channels;               
        uint8_t update_config  ;         
    } to_update;
} _dac53202_data_t;


dac53202_handle_t dac53202_new(uint8_t i2c_address);

esp_err_t dac53202_preset_cfg(dac53202_handle_t handle, uint8_t channel_mask, uint8_t power_on_mask, bool update_now);

esp_err_t dac53202_set_voltage_raw(dac53202_handle_t handle, uint8_t channel_mask, uint16_t raw_value, bool update_now);

esp_err_t dac53202_set_voltage_mv(dac53202_handle_t handle, uint8_t channel_mask, uint16_t voltage_mv, bool update_now);

esp_err_t dac53202_get_voltage_mv(dac53202_handle_t handle, uint8_t channel, uint16_t *voltage_mv);