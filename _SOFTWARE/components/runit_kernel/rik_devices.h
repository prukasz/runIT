#pragma once
#include "status.h"


/**
 * @brief Register GPIO ESP provider with manager_io
 */
status_rep_t rik_p_gpio_esp_start(void);

/**
 * @brief First create device handle and structure, then add to manager and init wrapper if exist
 * @param i2c_addres: I2C address of the device to start
 * @param bus_num: I2C bus number (0 or 1) 
 */
status_rep_t rik_p_gpio_expander_start(uint8_t i2c_addres, bool bus_num);
/**
 * @brief First create device handle and structure, then add to manager and init wrapper if exist
 * @param i2c_addres: I2C address of the device to start
 * @param bus_num: I2C bus number (0 or 1) 
 */
status_rep_t rik_current_monitor_start(uint8_t i2c_addres, bool bus_num);

/**
 * @brief First create device handle and structure, then add to manager and init wrapper if exist
 * @param i2c_addres: I2C address of the device to start
 * @param bus_num: I2C bus number (0 or 1) 
 */
status_rep_t rik_p_vreg_start(uint8_t i2c_adders_0, uint8_t i2c_adders_1, bool bus_num);


status_rep_t rik_adc_expander_start(uint8_t i2c_addres, bool bus_num);

status_rep_t p_pwm_expadner_start(uint8_t i2c_addres, bool bus_num);

/**
 * @brief First create device handle and structure, then add to manager and init wrapper if exist
 * @param bus_num: I2C bus number (0 or 1) 
 */
status_rep_t rik_p_power_delivery_start(uint8_t i2c_addr, bool bus_num);


status_rep_t rik_p_dac_expander_start(uint8_t i2c_addr, bool bus_num);
