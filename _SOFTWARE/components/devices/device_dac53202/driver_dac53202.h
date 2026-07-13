#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include "sys_i2c.h"

#define DAC53202_VREF_MV 3500

typedef struct _dac53202_data_t {
  sys_i2c_driver_header_t header; 
  uint16_t                common_config;       
  uint16_t                channel_raw_value[2]; 
} _dac53202_data_t;

typedef _dac53202_data_t* dac53202_handle_t;

dac53202_handle_t dac53202_new(uint8_t i2c_address, bool i2c_bus_num);
void dac53202_delete(dac53202_handle_t handle);

esp_err_t dac53202_preset_cfg(dac53202_handle_t handle, uint8_t channel_mask, uint8_t power_on_mask);
esp_err_t dac53202_set_voltage_raw(dac53202_handle_t handle, uint8_t channel_mask, uint16_t raw_value);
esp_err_t dac53202_set_voltage_mv(dac53202_handle_t handle, uint8_t channel_mask, uint16_t voltage_mv);
esp_err_t dac53202_get_voltage_mv(dac53202_handle_t handle, uint8_t channel, uint16_t *voltage_mv);