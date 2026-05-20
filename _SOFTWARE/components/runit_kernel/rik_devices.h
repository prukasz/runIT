#pragma once
#include "status.h"


/**
 * @brief First create device handle and structure, then add to manager and init wrapper if exist
 * @param i2c_addres: I2C address of the device to start
 * @param bus_num: I2C bus number (0 or 1) 
 */
status_rep_t rik_gpio_expander_start(uint8_t i2c_addres, bool bus_num);
/**
 * @brief First create device handle and structure, then add to manager and init wrapper if exist
 * @param i2c_addres: I2C address of the device to start
 * @param bus_num: I2C bus number (0 or 1) 
 */
status_rep_t rik_i2c_start_ina3221(uint8_t i2c_addres, bool bus_num);

/**
 * @brief First create device handle and structure, then add to manager and init wrapper if exist
 * @param i2c_addres: I2C address of the device to start
 * @param bus_num: I2C bus number (0 or 1) 
 */
status_rep_t rik_i2c_start_tps55289(uint8_t i2c_adders_0, uint8_t i2c_adders_1, bool bus_num);


