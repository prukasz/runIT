#include "driver_dac53202.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

static const char* TAG = __FILE_NAME__;

#undef OWNER
#define OWNER OWNER_DRIVER_DAC53202

#define DAC53202_REG_COMMON_CONFIG  0x01
#define DAC53202_REG_DAC_CH0_DATA   0x1C
#define DAC53202_REG_DAC_CH1_DATA   0x1F

#define CHECK_HANDLE(h) do { if (!(h)) return ESP_ERR_INVALID_ARG; } while(0)

static esp_err_t _dac53202_write_reg(dac53202_handle_t handle, uint8_t reg, uint16_t data)
{
  uint8_t buf[3] = { reg, (uint8_t)(data >> 8), (uint8_t)(data & 0xFF) };
  return sys_i2c_master_transmit(handle, buf, sizeof(buf));
}

dac53202_handle_t dac53202_new(uint8_t i2c_address, bool i2c_bus_num)
{
  dac53202_handle_t handle = calloc(1, sizeof(_dac53202_data_t));
  if (!handle) {
    ESP_LOGE(TAG, "Failed to allocate memory for DAC53202 handle");
    return NULL;
  }

  handle->header.i2c_device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  handle->header.i2c_device_config.device_address = i2c_address;
  handle->header.i2c_device_config.scl_speed_hz = 400000; 
  handle->header.bus_num = i2c_bus_num;

  return handle;
}

void dac53202_delete(dac53202_handle_t handle)
{
  if (handle) {
    free(handle);
  }
}

esp_err_t dac53202_preset_cfg(dac53202_handle_t handle, uint8_t channel_mask, uint8_t power_on_mask)
{
  CHECK_HANDLE(handle);
  handle->common_config = (channel_mask << 8) | power_on_mask;
  return _dac53202_write_reg(handle, DAC53202_REG_COMMON_CONFIG, handle->common_config);
}

esp_err_t dac53202_set_voltage_raw(dac53202_handle_t handle, uint8_t channel_mask, uint16_t raw_value)
{
  CHECK_HANDLE(handle);

  if (channel_mask & 0x01) {
    handle->channel_raw_value[0] = raw_value;
    esp_err_t err = _dac53202_write_reg(handle, DAC53202_REG_DAC_CH0_DATA, raw_value);
    if (err != ESP_OK) return err;
  }
  if (channel_mask & 0x02) {
    handle->channel_raw_value[1] = raw_value;
    esp_err_t err = _dac53202_write_reg(handle, DAC53202_REG_DAC_CH1_DATA, raw_value);
    if (err != ESP_OK) return err;
  }

  return ESP_OK;
}

esp_err_t dac53202_set_voltage_mv(dac53202_handle_t handle, uint8_t channel_mask, uint16_t voltage_mv)
{
  CHECK_HANDLE(handle);
  
  if (voltage_mv > DAC53202_VREF_MV) {
    voltage_mv = DAC53202_VREF_MV;
  }

  uint32_t raw_value_12bit = ((uint32_t)voltage_mv * 4095) / DAC53202_VREF_MV;
  uint16_t reg_formatted_val = (uint16_t)(raw_value_12bit << 4);

  return dac53202_set_voltage_raw(handle, channel_mask, reg_formatted_val);
}

esp_err_t dac53202_get_voltage_mv(dac53202_handle_t handle, uint8_t channel, uint16_t *voltage_mv)
{
  CHECK_HANDLE(handle);
  CHECK_HANDLE(voltage_mv);
  if (channel > 1) return ESP_ERR_INVALID_ARG;

  uint16_t raw_value_12bit = handle->channel_raw_value[channel] >> 4;
  *voltage_mv = (uint16_t)(((uint32_t)raw_value_12bit * DAC53202_VREF_MV) / 4095);
  return ESP_OK;
}