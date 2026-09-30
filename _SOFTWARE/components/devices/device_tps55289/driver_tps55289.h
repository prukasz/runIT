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
  uint16_t shunt_resistor_mohm;
  uint8_t reg_cache[8];
  struct {
    uint8_t raw_status_reg;
    bool scp;
    bool ocp;
    bool ovp;
    uint8_t op_mode;
  } last_status;
} _tps55289_data_t;

typedef _tps55289_data_t *tps55289_handle_t;

tps55289_handle_t tps55289_new(uint8_t i2c_address, bool i2c_bus_num);
void tps55289_delete(tps55289_handle_t handle);

void tps55289_set_shunt_resistor(tps55289_handle_t handle,
                                 uint16_t resistance_mOhm);
esp_err_t tps55289_set_output_enable(tps55289_handle_t handle, bool enable);
esp_err_t tps55289_set_current_limit(tps55289_handle_t handle, bool enable,
                                     uint16_t limit_mA);
esp_err_t tps55289_set_voltage(tps55289_handle_t handle, uint16_t voltage_mV);
/* VOUT_SR[1:0]: slew rate of output voltage changes (reset default 2.5 mV/us). */
typedef enum {
  TPS55289_SLEW_1_25_MV_US = 0,
  TPS55289_SLEW_2_5_MV_US = 1,
  TPS55289_SLEW_5_MV_US = 2,
  TPS55289_SLEW_10_MV_US = 3,
} tps55289_slew_rate_e;
esp_err_t tps55289_set_slew_rate(tps55289_handle_t handle, tps55289_slew_rate_e rate);
esp_err_t tps55289_set_mode(tps55289_handle_t handle, bool fpwm, bool hiccup);
/* true = the fault drives the FB/INT pin low (and sets its STATUS bit). */
esp_err_t tps55289_set_fault_reporting(tps55289_handle_t handle, bool report_scp,
                                       bool report_ocp, bool report_ovp);

esp_err_t tps55289_get_status(tps55289_handle_t handle);