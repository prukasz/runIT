#pragma once
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdbool.h>
#include <stdint.h>
#include "sys_i2c.h"

typedef enum {
  TCA_ON_RISING_EDGE = 0,
  TCA_ON_FALLING_EDGE = 1,
  TCA_ON_CHANGE = 2,
} tca_interrupt_mode_e;

typedef struct {
  sys_i2c_driver_header_t header;

  uint8_t last_read_input[3];
  uint8_t output[3];
  uint8_t polarity_cfg[3];
  uint8_t config[3];
} tca_data_t;

typedef tca_data_t* tca6424a_handle_t;

esp_err_t tca_set_pins(tca6424a_handle_t handle, uint32_t pins_mask, uint32_t pins_state);
esp_err_t tca_preset_cfg(tca6424a_handle_t handle, uint32_t cfg_mask, uint32_t cfg_state);
esp_err_t tca_set_polarity(tca6424a_handle_t handle, uint32_t polarity_mask, uint32_t polarity_state);
esp_err_t tca_get_pins(tca6424a_handle_t handle, uint32_t* out_level);
uint32_t tca_get_pin_output(tca6424a_handle_t handle);
esp_err_t tca_restore_state(tca6424a_handle_t handle);

tca6424a_handle_t d_tca6424a_new(uint8_t i2c_address, bool i2c_bus_num);
void d_tca6424a_delete(tca6424a_handle_t handle);