#include "sys_i2c.h"
#include "esp_log.h"
#include "sys_i2c_types.h"

static const char* TAG = __FILE_NAME__;

static i2c_master_bus_config_t manager_bus_cfg_0;
static i2c_master_bus_config_t manager_bus_cfg_1;
static i2c_master_bus_handle_t bus_handle_0;
static i2c_master_bus_handle_t bus_handle_1;

status_rep_t sys_i2c_init(i2c_master_bus_config_t* bus0_config, i2c_master_bus_config_t* bus1_config) {
#undef OWNER
#define OWNER OWNER_SYS_I2C_INIT
  CHECK_NOT_NULL_RP(bus0_config);
  memcpy(&manager_bus_cfg_0, bus0_config, sizeof(i2c_master_bus_config_t));
  CHECK_ESP_CALL_RP(i2c_new_master_bus(&manager_bus_cfg_0, &bus_handle_0));
  ESP_LOGI(TAG, "Successfully created bus 0, sda: %d, scl %d", manager_bus_cfg_0.sda_io_num, manager_bus_cfg_0.scl_io_num);

#undef OWNER
#define OWNER OWNER_SYS_I2C_INIT
  CHECK_NOT_NULL_RP(bus1_config);
  memcpy(&manager_bus_cfg_1, bus1_config, sizeof(i2c_master_bus_config_t));
  CHECK_ESP_CALL_RP(i2c_new_master_bus(&manager_bus_cfg_1, &bus_handle_1));
  ESP_LOGI(TAG, "Successfully created bus 1, sda: %d, scl %d", manager_bus_cfg_1.sda_io_num, manager_bus_cfg_1.scl_io_num);
  return STA_OK;
}

i2c_master_bus_handle_t sys_i2c_get_bus_handle(bool bus_num) {
  return (bus_num == 0) ? bus_handle_0 : bus_handle_1;
}

#undef OWNER
#define OWNER OWNER_SYS_I2C_ADD_DRIVER
status_rep_t sys_i2c_add_driver(void* device_handle) {
  CHECK_HANDLE_RP(device_handle);
  sys_i2c_driver_header_t* header = (sys_i2c_driver_header_t*)device_handle;
  i2c_master_bus_handle_t master_bus = header->bus_num ? bus_handle_1 : bus_handle_0;
  CHECK_ESP_CALL_RP(i2c_master_bus_add_device(master_bus, &header->i2c_device_config, &header->i2c_master_dev_handle));

  header->transmit = sys_i2c_master_transmit;
  header->transmit_receive = sys_i2c_master_transmit_receive;

  ESP_LOGI(TAG, "To bus %d added device with address 0x%02X", header->bus_num, header->i2c_device_config.device_address);
  return STA_OK;
}

#undef OWNER
#define OWNER OWNER_SYS_I2C_REMOVE_DRIVER
status_rep_t sys_i2c_remove_driver(void* device_handle) {
  CHECK_HANDLE_RP(device_handle);
  sys_i2c_driver_header_t* header = (sys_i2c_driver_header_t*)device_handle;
  CHECK_ESP_CALL_RP(i2c_master_bus_rm_device(header->i2c_master_dev_handle));
  return STA_OK;
}

#undef OWNER
#define OWNER OWNER_SYS_I2C_DEVICE_PRESENT
status_rep_t sys_i2c_device_present(void* device_handle) {
  CHECK_HANDLE_R(device_handle);
  sys_i2c_driver_header_t* header = (sys_i2c_driver_header_t*)device_handle;
  i2c_master_bus_handle_t master_bus = header->bus_num ? bus_handle_1 : bus_handle_0;
  esp_err_t err = i2c_master_probe(master_bus, header->i2c_device_config.device_address, 100);
  if (err) {
    ESP_LOGW(TAG, "Device at address 0x%02X not found on bus %d, (%s)", header->i2c_device_config.device_address, header->bus_num ? 1 : 0, esp_err_to_name(err));
    return STA_W(ERR_I2C_DEV_NOT_FOUND, OWNER, header->i2c_device_config.device_address, STATUS_PAYLOAD_UNKNOWN);
  }
  ESP_LOGI(TAG, "Device found at address 0x%02X on bus %d", header->i2c_device_config.device_address, header->bus_num ? 1 : 0);
  return STA_OK;
}

esp_err_t sys_i2c_master_transmit(void* device_handle, const uint8_t* write_buffer, size_t write_size) {
  if (!device_handle) {
    STA_P(STA_C(ERR_MISSING_HANDLE, OWNER_SYS_I2C_MASTER_TRANSMIT, 0, STATUS_PAYLOAD_UNKNOWN));
    return ESP_ERR_INVALID_ARG;
  }

  sys_i2c_driver_header_t* header = (sys_i2c_driver_header_t*)device_handle;

  esp_err_t err = i2c_master_transmit(header->i2c_master_dev_handle, write_buffer, write_size, 50);

  if (err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_TIMEOUT) {
    err = i2c_master_transmit(header->i2c_master_dev_handle, write_buffer, write_size, 50);
  }

  if (err != ESP_OK) {
    STA_P(STA_C(ERR_I2C_TRANSMISSION_FAILURE, OWNER_SYS_I2C_MASTER_TRANSMIT, (uint64_t)(uintptr_t)device_handle, STATUS_PAYLOAD_UNKNOWN));
    return err;
  }

  return ESP_OK;
}

esp_err_t sys_i2c_master_transmit_receive(void* device_handle, const uint8_t* write_buffer, size_t write_size, uint8_t* read_buffer, size_t read_size) {
  if (!device_handle) {
    STA_P(STA_C(ERR_MISSING_HANDLE, OWNER_SYS_I2C_MASTER_TRANSMIT_RECEIVE, 0, STATUS_PAYLOAD_UNKNOWN));
    return ESP_ERR_INVALID_ARG;
  }

  sys_i2c_driver_header_t* header = (sys_i2c_driver_header_t*)device_handle;

  // 1. First Attempt
  esp_err_t err = i2c_master_transmit_receive(header->i2c_master_dev_handle, write_buffer, write_size, read_buffer, read_size, 50);

  // 2. Retry on transient errors
  if (err == ESP_ERR_INVALID_RESPONSE || err == ESP_ERR_TIMEOUT) {
    err = i2c_master_transmit_receive(header->i2c_master_dev_handle, write_buffer, write_size, read_buffer, read_size, 50);
  }

  // 3. Catch-All Failure Check
  if (err != ESP_OK) {
    STA_P(STA_C(ERR_I2C_TRANSMISSION_FAILURE, OWNER_SYS_I2C_MASTER_TRANSMIT_RECEIVE, (uint64_t)(uintptr_t)device_handle, STATUS_PAYLOAD_UNKNOWN));
    return err;
  }

  return ESP_OK;
}
