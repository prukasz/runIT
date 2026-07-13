#pragma once
#include "sys_i2c.h"

#define PCA9685_I2C_DEFAULT_FREQUENCY   100000
#define PCA9685_MAX_PWM_VALUE           4095   
#define PCA9685_CHANNEL_ALL             16

typedef struct _pca9685_data_t {
    sys_i2c_driver_header_t header;
    uint16_t freq;     
    uint8_t prescale;  
    uint16_t channel_pwm_value[PCA9685_CHANNEL_ALL]; 
} _pca9685_data_t;

typedef struct _pca9685_data_t* pca9685_handle_t;

pca9685_handle_t pca9685_new(uint8_t i2c_address, bool i2c_bus_num);
esp_err_t pca9685_start(pca9685_handle_t handle);
esp_err_t pca9685_set_pwm_value(pca9685_handle_t handle, uint8_t channel, uint16_t value);
esp_err_t pca9685_set_pwm_frequency(pca9685_handle_t handle, uint16_t freq);
esp_err_t pca9685_sleep(pca9685_handle_t handle, bool sleep);
esp_err_t pca9685_enable_auto_increment(pca9685_handle_t handle);