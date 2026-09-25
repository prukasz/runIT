#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>
#include "sys_i2c.h"

/* Full scale: both channels use VDD as reference at gain 1x (3.3 V board supply). */
#define DAC53202_VREF_MV 3300

typedef struct _dac53202_data_t {
  sys_i2c_driver_header_t header;
  uint8_t                 power_on_mask;       /* bit per channel: VOUT powered up */
  uint16_t                channel_raw_value[2];
} _dac53202_data_t;

typedef _dac53202_data_t* dac53202_handle_t;

dac53202_handle_t dac53202_new(uint8_t i2c_address, bool i2c_bus_num);
void dac53202_delete(dac53202_handle_t handle);

/* Select the VDD reference on both channels and power them down (Hi-Z). */
esp_err_t dac53202_start(dac53202_handle_t handle);
/* Power the VOUT of each channel in the mask up; the others down (Hi-Z). */
esp_err_t dac53202_set_power(dac53202_handle_t handle, uint8_t power_on_mask);
esp_err_t dac53202_set_voltage_raw(dac53202_handle_t handle, uint8_t channel_mask, uint16_t raw_value);
/* Writes the code and powers the channels in the mask up. */
esp_err_t dac53202_set_voltage_mV(dac53202_handle_t handle, uint8_t channel_mask, uint16_t voltage_mV);
esp_err_t dac53202_get_voltage_mV(dac53202_handle_t handle, uint8_t channel, uint16_t *voltage_mV);
