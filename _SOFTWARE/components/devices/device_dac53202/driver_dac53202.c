#include "driver_dac53202.h"
#include <string.h>
#include <stdlib.h>

/* Register map: DAC53202 datasheet (SLASF47) §7.6. Channel 0 and 1 registers
   are not in address order. */
#define DAC53202_REG_DAC_1_VOUT_CMP_CONFIG 0x03
#define DAC53202_REG_DAC_0_VOUT_CMP_CONFIG 0x15
#define DAC53202_REG_DAC_1_DATA 0x19
#define DAC53202_REG_DAC_0_DATA 0x1C
#define DAC53202_REG_COMMON_CONFIG 0x1F

/* COMMON-CONFIG: reset 0x0FFF (both channels powered down, Hi-Z). Per channel
   VOUT-PDN (2 bits) + IOUT-PDN (1 bit): channel 0 at [11:9], channel 1 at [2:0].
   The don't-care bits [8:3] are written with their reset value. */
#define DAC53202_COMMON_DONT_CARE 0x01F8
#define DAC53202_PDN_CH0 0x0E00
#define DAC53202_PDN_CH1 0x0007
#define DAC53202_VOUT_ON_CH0 0x0200  // VOUT powered up, IOUT still down
#define DAC53202_VOUT_ON_CH1 0x0001

/* VOUT-GAIN [12:10] = 001: gain 1x, VDD as reference (full scale = VDD). */
#define DAC53202_VOUT_GAIN_VDD (1u << 10)

#define CHECK_DRV_HANDLE(h) do { if (!(h)) return ESP_ERR_INVALID_ARG; } while(0)

static esp_err_t _dac53202_write_reg(dac53202_handle_t handle, uint8_t reg, uint16_t data)
{
  uint8_t buf[3] = { reg, (uint8_t)(data >> 8), (uint8_t)(data & 0xFF) };
  return sys_i2c_master_transmit(handle, buf, sizeof(buf));
}

dac53202_handle_t dac53202_new(uint8_t i2c_address, bool i2c_bus_num)
{
  dac53202_handle_t handle = calloc(1, sizeof(_dac53202_data_t));
  if (!handle) {
    return NULL;
  }

  handle->header.i2c_device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  handle->header.i2c_device_config.device_address = i2c_address;
  handle->header.i2c_device_config.scl_speed_hz = 400000;
  handle->header.bus_num = i2c_bus_num;
  handle->power_on_mask = 0;

  return handle;
}

void dac53202_delete(dac53202_handle_t handle)
{
  if (handle) {
    free(handle);
  }
}

esp_err_t dac53202_start(dac53202_handle_t handle)
{
  CHECK_DRV_HANDLE(handle);
  esp_err_t err = _dac53202_write_reg(handle, DAC53202_REG_DAC_0_VOUT_CMP_CONFIG, DAC53202_VOUT_GAIN_VDD);
  if (err != ESP_OK) return err;
  err = _dac53202_write_reg(handle, DAC53202_REG_DAC_1_VOUT_CMP_CONFIG, DAC53202_VOUT_GAIN_VDD);
  if (err != ESP_OK) return err;
  return dac53202_set_power(handle, 0x00);
}

esp_err_t dac53202_set_power(dac53202_handle_t handle, uint8_t power_on_mask)
{
  CHECK_DRV_HANDLE(handle);
  uint16_t reg = DAC53202_COMMON_DONT_CARE;
  reg |= (power_on_mask & 0x01) ? DAC53202_VOUT_ON_CH0 : DAC53202_PDN_CH0;
  reg |= (power_on_mask & 0x02) ? DAC53202_VOUT_ON_CH1 : DAC53202_PDN_CH1;
  esp_err_t err = _dac53202_write_reg(handle, DAC53202_REG_COMMON_CONFIG, reg);
  if (err == ESP_OK) handle->power_on_mask = power_on_mask & 0x03;
  return err;
}

esp_err_t dac53202_set_voltage_raw(dac53202_handle_t handle, uint8_t channel_mask, uint16_t raw_value)
{
  CHECK_DRV_HANDLE(handle);

  if (channel_mask & 0x01) {
    handle->channel_raw_value[0] = raw_value;
    esp_err_t err = _dac53202_write_reg(handle, DAC53202_REG_DAC_0_DATA, raw_value);
    if (err != ESP_OK) return err;
  }
  if (channel_mask & 0x02) {
    handle->channel_raw_value[1] = raw_value;
    esp_err_t err = _dac53202_write_reg(handle, DAC53202_REG_DAC_1_DATA, raw_value);
    if (err != ESP_OK) return err;
  }

  return ESP_OK;
}

esp_err_t dac53202_set_voltage_mV(dac53202_handle_t handle, uint8_t channel_mask, uint16_t voltage_mV)
{
  CHECK_DRV_HANDLE(handle);

  if (voltage_mV > DAC53202_VREF_MV) {
    voltage_mV = DAC53202_VREF_MV;
  }

  // Data is MSB-aligned in [15:4] (DAC63202 12 bit; DAC53202 uses the top 10).
  uint32_t raw_value_12bit = ((uint32_t)voltage_mV * 4095) / DAC53202_VREF_MV;
  uint16_t reg_formatted_val = (uint16_t)(raw_value_12bit << 4);

  esp_err_t err = dac53202_set_voltage_raw(handle, channel_mask, reg_formatted_val);
  if (err != ESP_OK) return err;
  // A written channel is powered up (VOUT) if it was down.
  uint8_t next = handle->power_on_mask | (channel_mask & 0x03);
  return next == handle->power_on_mask ? ESP_OK : dac53202_set_power(handle, next);
}

esp_err_t dac53202_get_voltage_mV(dac53202_handle_t handle, uint8_t channel, uint16_t *voltage_mV)
{
  CHECK_DRV_HANDLE(handle);
  CHECK_DRV_HANDLE(voltage_mV);
  if (channel > 1) return ESP_ERR_INVALID_ARG;

  uint16_t raw_value_12bit = handle->channel_raw_value[channel] >> 4;
  *voltage_mV = (uint16_t)(((uint32_t)raw_value_12bit * DAC53202_VREF_MV) / 4095);
  return ESP_OK;
}
