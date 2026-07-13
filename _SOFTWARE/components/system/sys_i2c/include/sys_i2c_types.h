#pragma once
#include "driver/i2c_master.h"

typedef struct sys_i2c_driver_header_t {
    i2c_master_dev_handle_t i2c_master_dev_handle;
    i2c_device_config_t     i2c_device_config;
    
    i2c_port_t              bus_num; 
        
    // Zmieniamy na void* ctx !
    esp_err_t (*transmit)(void* ctx, 
                        const uint8_t *write_buffer, 
                        size_t write_size);
                        
    esp_err_t (*transmit_receive)(void* ctx, 
                                const uint8_t *write_buffer, 
                                size_t write_size, 
                                uint8_t *read_buffer, 
                                size_t read_size);
}sys_i2c_driver_header_t;

esp_err_t sys_i2c_master_transmit(void* device_handle, const uint8_t *write_buffer, size_t write_size);
esp_err_t sys_i2c_master_transmit_receive(void* device_handle, const uint8_t *write_buffer, size_t write_size, uint8_t *read_buffer, size_t read_size);