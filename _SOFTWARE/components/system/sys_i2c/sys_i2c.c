#include "sys_i2c.h"
#include "sys_error.h"

static i2c_master_bus_handle_t s_bus_handles[2] = {NULL, NULL};

#undef OWNER
#define OWNER OWNER_SYS_I2C_INIT
err_h sys_i2c_init(i2c_master_bus_config_t* bus0_config, i2c_master_bus_config_t* bus1_config) {
  if (bus0_config != NULL) {
    SE_TRY_ESP(i2c_new_master_bus(bus0_config, &s_bus_handles[0]));
  }
  if (bus1_config != NULL) {
    SE_TRY_ESP(i2c_new_master_bus(bus1_config, &s_bus_handles[1]));
  }
  return NULL;
}

i2c_master_bus_handle_t sys_i2c_get_bus_handle(bool bus_num) {
  return s_bus_handles[bus_num ? 1 : 0];
}

#undef OWNER
#define OWNER OWNER_SYS_I2C_ADD_DRIVER
err_h sys_i2c_add_driver(void* hw_handle) {
  SE_CHECK_NOT_NULL(hw_handle);
  sys_i2c_driver_header_t* header = (sys_i2c_driver_header_t*)hw_handle;

  i2c_master_bus_handle_t bus = sys_i2c_get_bus_handle(header->bus_num == SYS_I2C_BUS1);
  if (!bus) {
    SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  }
  SE_TRY(sys_i2c_device_present(hw_handle));
  SE_TRY_ESP(i2c_master_bus_add_device(bus, &header->i2c_device_config, &header->i2c_master_dev_handle));
  header->transmit = sys_i2c_master_transmit;
  header->transmit_receive = sys_i2c_master_transmit_receive;
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_I2C_REMOVE_DRIVER
err_h sys_i2c_remove_driver(void* hw_handle) {
  SE_CHECK_NOT_NULL(hw_handle);
  sys_i2c_driver_header_t* header = (sys_i2c_driver_header_t*)hw_handle;
  if (header->i2c_master_dev_handle) {
    esp_err_t esp_err = i2c_master_bus_rm_device(header->i2c_master_dev_handle);
    header->i2c_master_dev_handle = NULL;
    SE_TRY_ESP(esp_err);
  }
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_I2C_DEVICE_PRESENT
err_h sys_i2c_device_present(void* hw_handle) {
  SE_CHECK_NOT_NULL(hw_handle);
  sys_i2c_driver_header_t* header = (sys_i2c_driver_header_t*)hw_handle;
  i2c_master_bus_handle_t bus = sys_i2c_get_bus_handle(header->bus_num == SYS_I2C_BUS1);
  if (!bus) {
    SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  }
  esp_err_t err = i2c_master_probe(bus, header->i2c_device_config.device_address, 50);
  if (err != ESP_OK) {
    SE_FAIL(ERR_I2C_DEV_NOT_FOUND, header->i2c_device_config.device_address);
  }
  return NULL;
}

#include <string.h>
#include <sdkconfig.h>

esp_err_t sys_i2c_master_transmit(void* hw_handle, const uint8_t* write_buffer, size_t write_size) {
  if (!hw_handle) return ESP_ERR_INVALID_ARG;
  sys_i2c_driver_header_t* header = (sys_i2c_driver_header_t*)hw_handle;
  if (!header->i2c_master_dev_handle) return ESP_ERR_INVALID_STATE;
  return i2c_master_transmit(header->i2c_master_dev_handle, write_buffer, write_size, CONFIG_SYS_I2C_DEFAULT_TIMEOUT_MS);
}

esp_err_t sys_i2c_master_transmit_receive(void* hw_handle, const uint8_t* write_buffer, size_t write_size, uint8_t* read_buffer, size_t read_size) {
  if (!hw_handle) return ESP_ERR_INVALID_ARG;
  sys_i2c_driver_header_t* header = (sys_i2c_driver_header_t*)hw_handle;
  if (!header->i2c_master_dev_handle) return ESP_ERR_INVALID_STATE;
  return i2c_master_transmit_receive(header->i2c_master_dev_handle, write_buffer, write_size, read_buffer, read_size, CONFIG_SYS_I2C_DEFAULT_TIMEOUT_MS);
}

#undef OWNER
#define OWNER OWNER_SYS_I2C_DEV_ADD
err_h sys_i2c_dev_add(sys_i2c_dev_t* dev) {
  SE_CHECK_NOT_NULL(dev);
  i2c_master_bus_handle_t bus = sys_i2c_get_bus_handle(dev->bus1);
  if (!bus) {
    SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  }
  if (i2c_master_probe(bus, dev->addr, 50) != ESP_OK) {
    SE_FAIL(ERR_I2C_DEV_NOT_FOUND, dev->addr);
  }
  const i2c_device_config_t cfg = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = dev->addr,
      .scl_speed_hz = dev->speed_hz,
  };
  SE_TRY_ESP(i2c_master_bus_add_device(bus, &cfg, &dev->handle));
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_I2C_DEV_REMOVE
err_h sys_i2c_dev_remove(sys_i2c_dev_t* dev) {
  SE_CHECK_NOT_NULL(dev);
  if (dev->handle) {
    esp_err_t esp_err = i2c_master_bus_rm_device(dev->handle);
    dev->handle = NULL;
    SE_TRY_ESP(esp_err);
  }
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_I2C_WRITE
err_h sys_i2c_write(sys_i2c_dev_t* dev, const uint8_t* write_buffer, size_t write_size) {
  SE_CHECK_NOT_NULL(dev);
  if (!dev->handle) {
    SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  }
  SE_TRY_ESP(i2c_master_transmit(dev->handle, write_buffer, write_size, CONFIG_SYS_I2C_DEFAULT_TIMEOUT_MS));
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_I2C_WRITE_READ
err_h sys_i2c_write_read(sys_i2c_dev_t* dev, const uint8_t* write_buffer, size_t write_size, uint8_t* read_buffer, size_t read_size) {
  SE_CHECK_NOT_NULL(dev);
  if (!dev->handle) {
    SE_FAIL(ERR_BASE_INVALID_STATE, 0);
  }
  SE_TRY_ESP(i2c_master_transmit_receive(dev->handle, write_buffer, write_size, read_buffer, read_size, CONFIG_SYS_I2C_DEFAULT_TIMEOUT_MS));
  return NULL;
}

#undef OWNER
#define OWNER OWNER_SYS_I2C_REG
err_h sys_i2c_reg_write(sys_i2c_dev_t* dev, uint8_t reg, const uint8_t* data, size_t len) {
  SE_CHECK_NOT_NULL(data);
  SE_CHECK_IN_RANGE((uint32_t)len, 1, SYS_I2C_REG_MAX_LEN);
  uint8_t buf[1 + SYS_I2C_REG_MAX_LEN];  // stack: no heap churn in periodic tasks
  buf[0] = reg;
  memcpy(&buf[1], data, len);
  return sys_i2c_write(dev, buf, len + 1);
}

err_h sys_i2c_reg_read(sys_i2c_dev_t* dev, uint8_t reg, uint8_t* data, size_t len) {
  SE_CHECK_NOT_NULL(data);
  return sys_i2c_write_read(dev, &reg, 1, data, len);
}

err_h sys_i2c_reg_update(sys_i2c_dev_t* dev, uint8_t reg, uint8_t mask, uint8_t val) {
  uint8_t r = 0;
  SE_TRY(sys_i2c_reg_read(dev, reg, &r, 1));
  r = (r & ~mask) | (val & mask);
  return sys_i2c_reg_write(dev, reg, &r, 1);
}
