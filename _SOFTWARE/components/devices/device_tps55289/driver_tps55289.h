#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sys_i2c.h"
#include <stdbool.h>
#include <stdint.h>

#define TPS55289_REG_REF_LSB 0x00
#define TPS55289_REG_REF_MSB 0x01
#define TPS55289_REG_IOUT_LIMIT 0x02
#define TPS55289_REG_VOUT_SR 0x03
#define TPS55289_REG_VOUT_FS 0x04
#define TPS55289_REG_CDC 0x05
#define TPS55289_REG_MODE 0x06
#define TPS55289_REG_STATUS 0x07

#define TPS55289_I2C_ADDR_74 0x74
#define TPS55289_I2C_ADDR_75 0x75

typedef struct _tps55289_data_t {
  sys_i2c_driver_header_t header;
  TaskHandle_t driver_task;
  uint16_t shunt_resistor_mohm;
  uint8_t reg_cache[8];
  struct {
    uint8_t raw_status_reg;
    bool scp;
    bool ocp;
    bool ovp;
    uint8_t op_mode;
  } last_status;

  void (*on_fault_cb)(void *arg, bool ovp, bool ocp, bool scp);
  void *on_fault_arg;
} _tps55289_data_t;

typedef _tps55289_data_t *tps55289_handle_t;

tps55289_handle_t tps55289_new(uint8_t i2c_address, bool i2c_bus_num);
void tps55289_delete(tps55289_handle_t handle);

void tps55289_set_shunt_resistor(tps55289_handle_t handle,
                                 uint16_t resistance_mOhm);
esp_err_t tps55289_set_output_enable(tps55289_handle_t handle, bool enable);
esp_err_t tps55289_set_current_limit(tps55289_handle_t handle, bool enable,
                                     uint16_t limit_ma);
esp_err_t tps55289_set_voltage(tps55289_handle_t handle, uint16_t voltage_mv);
esp_err_t tps55289_set_mode(tps55289_handle_t handle, bool fpwm, bool hiccup);
esp_err_t tps55289_set_fault_masks(tps55289_handle_t handle, bool mask_scp,
                                   bool mask_ocp, bool mask_ovp);
void tps55289_register_on_fault_callback(tps55289_handle_t handle,
                                         void (*callback)(void *, bool, bool,
                                                          bool),
                                         void *arg);

void tps55289_isr_handler(tps55289_handle_t handle);