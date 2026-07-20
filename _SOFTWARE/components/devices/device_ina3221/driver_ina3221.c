#include "driver_ina3221.h"
#include <stdint.h>
#include <string.h>

#define TAG __FILE_NAME__

#undef OWNER
#define OWNER OWNER_DRIVER_INA3221

#define I2C_FREQ_HZ 400000

#define INA3221_REG_CONFIG (0x00)
#define INA3221_REG_SHUNTVOLTAGE_1 (0x01)
#define INA3221_REG_BUSVOLTAGE_1 (0x02)
#define INA3221_REG_CRITICAL_ALERT_1 (0x07)
#define INA3221_REG_WARNING_ALERT_1 (0x08)
#define INA3221_REG_SHUNT_VOLTAGE_SUM (0x0D)
#define INA3221_REG_SHUNT_VOLTAGE_SUM_LIMIT (0x0E)
#define INA3221_REG_MASK (0x0F)
#define INA3221_REG_VALID_POWER_UPPER_LIMIT (0x10)
#define INA3221_REG_VALID_POWER_LOWER_LIMIT (0x11)

#define RETURN_ON_ERROR(x)                   \
  do {                                       \
    esp_err_t __err_rc = (x);                \
    if (__err_rc != ESP_OK) return __err_rc; \
  } while (0)

#undef CHECK_HANDLE_R
#define CHECK_HANDLE_R(VAL)                 \
  do {                                      \
    if (!(VAL)) return ESP_ERR_INVALID_ARG; \
  } while (0)

static esp_err_t _ina3221_read(ina3221_handle_t handle, const uint8_t reg, uint16_t* val) {
  CHECK_HANDLE_R(val);
  RETURN_ON_ERROR(sys_i2c_master_transmit_receive(handle, (uint8_t[]){reg}, 1, (uint8_t*)val, 2));
  *val = (*val >> 8) | (*val << 8);  // Swap bytes
  return ESP_OK;
}

static esp_err_t _ina3221_write(ina3221_handle_t handle, uint8_t reg, uint16_t val) {
  CHECK_HANDLE_R(handle);
  uint8_t buf[3];
  buf[0] = reg;
  buf[1] = (val >> 8) & 0xFF;
  buf[2] = val & 0xFF;

  RETURN_ON_ERROR(sys_i2c_master_transmit(handle, buf, 3));
  return ESP_OK;
}

static inline esp_err_t write_config(ina3221_handle_t handle) {
  return _ina3221_write(handle, INA3221_REG_CONFIG, handle->config.config_register);
}

static inline esp_err_t write_mask(ina3221_handle_t handle) {
  return _ina3221_write(handle, INA3221_REG_MASK, handle->mask.mask_register & INA3221_MASK_CONFIG);
}

esp_err_t ina3221_get_status(ina3221_handle_t handle) {
  return _ina3221_read(handle, INA3221_REG_MASK, &handle->mask.mask_register);
}

esp_err_t ina3221_set_options(ina3221_handle_t handle, bool bus, bool mode, bool shunt_val_cfg) {
  handle->config.mode = mode;
  handle->config.ebus = bus;
  handle->config.esht = shunt_val_cfg;
  return write_config(handle);
}

esp_err_t ina3221_enable_channel(ina3221_handle_t handle, bool ch1, bool ch2, bool ch3) {
  handle->config.ch1 = ch1;
  handle->config.ch2 = ch2;
  handle->config.ch3 = ch3;
  return write_config(handle);
}

void ina3221_set_shunt_resistor(ina3221_handle_t handle, uint16_t resistance_mOhm, ina3221_channel_t channel) {
  if (channel == INA3221_CHANNEL_ALL) {
    handle->shunt_val_cfg[0] = resistance_mOhm;
    handle->shunt_val_cfg[1] = resistance_mOhm;
    handle->shunt_val_cfg[2] = resistance_mOhm;
    return;
  }
  handle->shunt_val_cfg[channel] = resistance_mOhm;
}

esp_err_t ina3221_enable_channel_sum(ina3221_handle_t handle, bool ch1, bool ch2, bool ch3) {
  handle->mask.scc1 = ch1;
  handle->mask.scc2 = ch2;
  handle->mask.scc3 = ch3;
  return write_mask(handle);
}

esp_err_t ina3221_enable_latch_pin(ina3221_handle_t handle, bool warning, bool critical) {
  handle->mask.wen = warning;
  handle->mask.cen = critical;
  return write_mask(handle);
}

esp_err_t ina3221_set_average(ina3221_handle_t handle, ina3221_avg_t avg) {
  handle->config.avg = avg;
  return write_config(handle);
}

esp_err_t ina3221_set_bus_conversion_time(ina3221_handle_t handle, ina3221_ct_t ct) {
  handle->config.vbus = ct;
  return write_config(handle);
}

esp_err_t ina3221_set_shunt_conversion_time(ina3221_handle_t handle, ina3221_ct_t ct) {
  handle->config.vsht = ct;
  return write_config(handle);
}

esp_err_t ina3221_reset(ina3221_handle_t handle) {
  handle->config.config_register = INA3221_DEFAULT_CONFIG;
  handle->mask.mask_register = INA3221_DEFAULT_MASK;
  handle->config.rst = 1;
  return write_config(handle);
}

esp_err_t ina3221_read_bus_voltage(ina3221_handle_t handle, uint8_t channel, int32_t* out_mv) {
  CHECK_HANDLE_R(handle);
  CHECK_HANDLE_R(out_mv);
  if (channel >= 3) return ESP_ERR_INVALID_ARG;

  int16_t raw;
  RETURN_ON_ERROR(_ina3221_read(handle, INA3221_REG_BUSVOLTAGE_1 + (channel * 2), (uint16_t*)&raw));
  raw = raw >> 3;
  *out_mv = (int32_t)(raw * 8.0f);  // 8mV -> LSB
  return ESP_OK;
}

esp_err_t ina3221_read_shunt_current(ina3221_handle_t handle, uint8_t channel, int32_t* out_ma) {
  CHECK_HANDLE_R(handle);
  CHECK_HANDLE_R(out_ma);
  if (channel >= 3) return ESP_ERR_INVALID_ARG;

  int16_t raw;
  RETURN_ON_ERROR(_ina3221_read(handle, INA3221_REG_SHUNTVOLTAGE_1 + (channel * 2), (uint16_t*)&raw));
  raw = raw >> 3;
  float mvolts = raw * 0.04f;  // 40uV -> LSB
  *out_ma = (int32_t)(mvolts * 1000.0f) / handle->shunt_val_cfg[channel];
  return ESP_OK;
}

esp_err_t ina3221_read_sum_shunt_voltage(ina3221_handle_t handle, float* out_mv) {
  CHECK_HANDLE_R(handle);
  CHECK_HANDLE_R(out_mv);

  int16_t raw;
  RETURN_ON_ERROR(_ina3221_read(handle, INA3221_REG_SHUNT_VOLTAGE_SUM, (uint16_t*)&raw));
  raw = raw >> 1;
  *out_mv = raw * 0.04f;  // 40uV -> LSB
  return ESP_OK;
}

esp_err_t ina3221_set_alert(ina3221_handle_t handle, ina3221_channel_t channel, int32_t current_mA, bool is_critical) {
  if (channel >= 3) return ESP_ERR_INVALID_ARG;
  float limit_mv = ((float)current_mA * handle->shunt_val_cfg[channel]) / 1000.0f;
  int16_t raw_count = (int16_t)(limit_mv / 0.04f);  // 40uV -> LSB

  uint16_t reg_val = ((uint16_t)raw_count) << 3;
  uint8_t alert_offset = is_critical ? INA3221_REG_CRITICAL_ALERT_1 : INA3221_REG_WARNING_ALERT_1;
  return _ina3221_write(handle, alert_offset + channel * 2, reg_val);
}

esp_err_t ina3221_set_sum_warning_alert(ina3221_handle_t handle, uint32_t voltage_mv) {
  int16_t raw_count = (int16_t)(voltage_mv / 0.04f);  // 40uV -> LSB
  uint16_t reg_val = raw_count << 1;
  return _ina3221_write(handle, INA3221_REG_SHUNT_VOLTAGE_SUM_LIMIT, reg_val);
}

ina3221_handle_t ina3221_new(uint8_t i2c_address, bool i2c_bus_num) {
  if (i2c_address < INA3221_I2C_ADDR_GND || i2c_address > INA3221_I2C_ADDR_SCL) {
    ESP_LOGE(TAG, "Invalid I2C address, must be between 0x40 and 0x43, provided: 0x%02x", i2c_address);
    return NULL;
  }
  ina3221_handle_t handle = calloc(1, sizeof(_ina3221_data_t));
  if (!handle) {
    ESP_LOGE(TAG, "Failed to allocate memory for INA3221 handle");
    return NULL;
  }

  handle->header.i2c_device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  handle->header.i2c_device_config.device_address = i2c_address;
  handle->header.i2c_device_config.scl_speed_hz = I2C_FREQ_HZ;
  handle->header.bus_num = i2c_bus_num;

  handle->mask.mask_register = INA3221_DEFAULT_MASK;
  handle->config.config_register = INA3221_DEFAULT_CONFIG;

  // Default shunt resistors to 10 mOhm
  handle->shunt_val_cfg[0] = 10;
  handle->shunt_val_cfg[1] = 10;
  handle->shunt_val_cfg[2] = 10;

  return handle;
}

void ina3221_delete(ina3221_handle_t handle) {
  free(handle);
}

esp_err_t ina3221_start(ina3221_handle_t handle) {
  if (!handle) return ESP_ERR_INVALID_ARG;

  status_rep_t init_status = sys_i2c_add_driver(&handle->header);
  if (STA_IS_ERR(init_status)) {
    ESP_LOGE(TAG, "I2C Manager rejected INA3221 on bus %d", handle->header.bus_num);
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "INA3221 started successfully on bus %d", handle->header.bus_num);
  return ESP_OK;
}
